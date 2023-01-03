/*
 * smart_power_driver.c
 *
 * Advanced driver for the Odroid Smart Power USB device
 *
 * Copyright (C) 2016 Adrian Garcia, Alvaro Sanz and Juan Carlos Saez
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * This driver is based on the sample driver found in the
 * Linux kernel sources  (drivers/usb/usb-skeleton.c)
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/ftrace.h>
#include <asm/delay.h>
#include <linux/ctype.h>
#include <pmc/data_str/cbuffer.h>
#include <pmc/smart_power.h>
#include <pmc/mc_experiments.h>
#include <linux/rwlock.h>

MODULE_LICENSE("GPL");

/* Get a minor range for your devices from the usb maintainer */
#define USB_SPOWER_MINOR_BASE	0
#define NR_BYTES_SPOWER_MSG 0x0040
#define SPOWER_INTERVAL 0x01

#define REQUEST_DATA        0x37
#define REQUEST_STARTSTOP   0x80
#define REQUEST_STATUS      0x81
#define REQUEST_ONOFF       0x82
#define REQUEST_VERSION     0x83

#define CBUFFER_CAPACITY	10
#define SPOWER_DEFAULT_TIMER_PERIOD	100
#define SPOWER_DEFAULT_SAMPLING_PERIOD	200

/* Structure to hold all of our device specific stuff */
struct usb_spower {
	struct usb_device	*udev;			/* the usb device for this device */
	struct usb_interface	*interface;		/* the interface for this device */
	struct kref		kref;
	struct urb *int_in_urb;
	struct urb *int_out_urb;
	struct usb_endpoint_descriptor *int_in_endpoint;
	struct usb_endpoint_descriptor *int_out_endpoint;
	char int_in_buffer[NR_BYTES_SPOWER_MSG];
	char int_out_buffer[NR_BYTES_SPOWER_MSG];
	unsigned int sampling_period_ms;
	spinlock_t lock;
};
#define to_spower_dev(d) container_of(d, struct usb_spower, kref)

static struct usb_driver spower_driver;

static int send_packet_spower (struct usb_spower *dev,unsigned char command);
static int send_packet_spower_noblock (struct usb_spower *dev,unsigned char command);
static int parse_spower_sample(const char* str, struct spower_sample* sample);


struct spower_ctl {
	struct usb_spower* dev;		/* Pointer to the device */
	cbuffer_t* cbuffer;			/* Sample buffer */
	struct timer_list timer; 	/* Kernel timer */
	unsigned int timer_period;	/* Period for the timer */
	unsigned char started;		/* Indicates if the timer has been started */
	unsigned long time_last_sample; /* Required for energy estimation */
	uint64_t cummulative_energy;	/* Counter to keep track of cumulative energy consumption */
	rwlock_t lock;				/* RW lock for the various fields */
} spower_gbl;

/*
 * Free up the usb_spower structure and
 * decrement the usage count associated with the usb device
 */
static void spower_delete(struct kref *kref)
{
	struct usb_spower *dev = to_spower_dev(kref);

	usb_put_dev(dev->udev);
	kfree(dev);
}

static void spower_int_out_callback(struct urb *urb)
{
	if (urb->status)
		if (urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN)
			trace_printk("Error submitting urb\n");
}

static void spower_int_in_callback(struct urb *urb)
{
	struct usb_spower *dev = urb->context;
	int retval = 0;
	struct spower_sample sample;
	unsigned long flags;
	unsigned long now=jiffies;

	if (urb->status) {
		if (urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN) {
			return;
		} else {
			goto resubmit; /* Maybe we can recover. */
		}
	}

	if (urb->actual_length > 0) {
		if(dev->int_in_buffer[0] == REQUEST_DATA) {
#ifdef DEBUG
			trace_printk("%.33s\n",dev->int_in_buffer);
#endif
			parse_spower_sample(dev->int_in_buffer,&sample);
			sample.timestamp=now;
			sample.m_ujoules=sample.m_watt*((now-spower_gbl.time_last_sample)*1000)/HZ;
			spower_gbl.cummulative_energy+=sample.m_ujoules;
			spower_gbl.time_last_sample=now;
			write_lock_irqsave(&spower_gbl.lock,flags);
			insert_items_cbuffer_t (spower_gbl.cbuffer,&sample,sizeof(struct spower_sample));
			write_unlock_irqrestore(&spower_gbl.lock,flags);
		}

	}

resubmit:
	retval = usb_submit_urb(dev->int_in_urb, GFP_ATOMIC);

}


/* Function invoked when the timer expires (fires) */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
static void spower_fire_timer(unsigned long data)
{
	struct spower_ctl* ctl=(struct spower_ctl*)	data;
#else
static void spower_fire_timer(struct timer_list *t)
{
	struct spower_ctl* ctl=container_of(t, struct spower_ctl, timer);
#endif
	unsigned long flags;

	write_lock_irqsave(&spower_gbl.lock,flags);

	if (ctl->dev) {
		if (send_packet_spower_noblock(ctl->dev,REQUEST_DATA)!=0)
			trace_printk("Error submitting packet\n");
	}
	write_unlock_irqrestore(&spower_gbl.lock,flags);

	/* Re-activate the timer... */
	mod_timer( &ctl->timer, jiffies + ctl->timer_period);
}

int spower_start_measurements(void)
{
	struct usb_spower *dev;
	int retval=0;
	unsigned long flags;

	write_lock_irqsave(&spower_gbl.lock,flags);
	if (spower_gbl.started || !spower_gbl.dev ) {
		retval=-ENODEV;
		goto unlock;
	}

	dev=spower_gbl.dev;
	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* Initialize interrupt URB. */
	usb_fill_int_urb(dev->int_in_urb, dev->udev,
	                 usb_rcvintpipe(dev->udev,
	                                dev->int_in_endpoint->bEndpointAddress),
	                 dev->int_in_buffer,
	                 le16_to_cpu(dev->int_in_endpoint->wMaxPacketSize),
	                 spower_int_in_callback,
	                 dev,
	                 dev->int_in_endpoint->bInterval);
	retval = usb_submit_urb(dev->int_in_urb, GFP_ATOMIC);

	if (retval!=0)
		goto unlock;

	/* Start timer */
	spower_gbl.timer.expires=jiffies+1; /* Start ASAP */
	spower_gbl.time_last_sample=spower_gbl.timer.expires-1;
	spower_gbl.cummulative_energy=0;
	add_timer(&spower_gbl.timer);

	spower_gbl.started=1;
unlock:
	write_unlock_irqrestore(&spower_gbl.lock,flags);
	return retval;
}


/* Stop gathering measurements from the device */
void spower_stop_measurements(void)
{
	struct usb_spower *dev;
	unsigned long flags;

	write_lock_irqsave(&spower_gbl.lock,flags);
	if (spower_gbl.started!=1 || !spower_gbl.dev)
		goto unlock;

	/* Transition state */
	spower_gbl.started=2;

	dev=spower_gbl.dev;
	write_unlock_irqrestore(&spower_gbl.lock,flags);

	if (dev->int_in_urb)
		usb_kill_urb(dev->int_in_urb);

	/* Cancel timer and wait for completion */
	del_timer_sync(&spower_gbl.timer);
	/* decrement the count on our device */
	kref_put(&dev->kref, spower_delete);

	write_lock_irqsave(&spower_gbl.lock,flags);
	/* Clear buffer */
	clear_cbuffer_t(spower_gbl.cbuffer);
	spower_gbl.started=0;
unlock:
	write_unlock_irqrestore(&spower_gbl.lock,flags);
}

/* Called when a user program invokes the open() system call on the device */
static int spower_open(struct inode *inode, struct file *file)
{
	struct usb_spower *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;
	unsigned long flags;

	/* Make sure that the MM is not active */
	if (spower_gbl.started)
		return -EBUSY;

	subminor = iminor(inode);

	/* Obtain reference to USB interface from minor number */
	interface = usb_find_interface(&spower_driver, subminor);
	if (!interface) {
		pr_err("%s - error, can't find device for minor %d\n",
		       __func__, subminor);
		return -ENODEV;
	}

	/* Obtain driver data associated with the USB interface */
	dev = usb_get_intfdata(interface);
	if (!dev)
		return -ENODEV;

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* save our object in the file's private structure */
	file->private_data = dev;

	if (! (file->f_mode & FMODE_READ))
		return 0;

	/* Initialize interrupt URB. */
	usb_fill_int_urb(dev->int_in_urb, dev->udev,
	                 usb_rcvintpipe(dev->udev,
	                                dev->int_in_endpoint->bEndpointAddress),
	                 dev->int_in_buffer,
	                 le16_to_cpu(dev->int_in_endpoint->wMaxPacketSize),
	                 spower_int_in_callback,
	                 dev,
	                 dev->int_in_endpoint->bInterval);
	retval = usb_submit_urb(dev->int_in_urb, GFP_KERNEL);

	if (retval!=0)
		return retval;

	/* Start timer */
	write_lock_irqsave(&spower_gbl.lock,flags);

	if (spower_gbl.dev==dev) {
		spower_gbl.timer.expires=jiffies+spower_gbl.timer_period;
		spower_gbl.time_last_sample=jiffies;
		spower_gbl.cummulative_energy=0;
		add_timer(&spower_gbl.timer);
	}

	write_unlock_irqrestore(&spower_gbl.lock,flags);


	return 0;
}

/* Called when a user program invokes the close() system call on the device */
static int spower_release(struct inode *inode, struct file *file)
{
	struct usb_spower *dev;
	unsigned long flags;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	if (file->f_mode & FMODE_READ) {
		if (dev->int_in_urb) {
			usb_kill_urb(dev->int_in_urb);
		}
		/* Cancel timer and wait for completion */
		del_timer_sync(&spower_gbl.timer);

		/* Clear buffer */
		write_lock_irqsave(&spower_gbl.lock,flags);
		clear_cbuffer_t(spower_gbl.cbuffer);
		write_unlock_irqrestore(&spower_gbl.lock,flags);
	}

	/* decrement the count on our device */
	kref_put(&dev->kref, spower_delete);

	return 0;
}


int send_packet_spower (struct usb_spower *dev,unsigned char command)
{
	int ret = 0;
	unsigned char message[NR_BYTES_SPOWER_MSG];
	int actual_len=0;

	/* zero fill*/
	memset(message,0,NR_BYTES_SPOWER_MSG);

	/* Fill up the message accordingly */
	message[0]=command;

	/* Send message (URB) */
	ret=usb_interrupt_msg(dev->udev,
	                      usb_sndintpipe(dev->udev,dev->int_out_endpoint->bEndpointAddress),
	                      message,	/* Pointer to the message */
	                      NR_BYTES_SPOWER_MSG, /* message's size in bytes */
	                      &actual_len,
	                      1000);

	if (ret < 0) {
		trace_printk("Couldn't send packet\n");
		return ret;
	} else if (actual_len != NR_BYTES_SPOWER_MSG)
		return -EIO;

	return 0;
}

int send_packet_spower_noblock(struct usb_spower *dev,unsigned char command)
{
	dev->int_out_buffer[0]=command;
	return usb_submit_urb(dev->int_out_urb,GFP_ATOMIC);
}

/* Operations to set/get the sampling period (specified in ms)*/
int spower_set_sampling_period(unsigned int ms)
{
	if (ms<50 || ms >4000)
		return -EINVAL;
	spower_gbl.timer_period=(HZ*ms)/1000;
	return 0;
}

unsigned int spower_get_sampling_period(void)
{
	return (1000*spower_gbl.timer_period)/HZ;
}

void spower_reset_energy_count(void)
{
	spower_gbl.cummulative_energy=0;
}

uint64_t spower_get_energy_count(void)
{
	return spower_gbl.cummulative_energy;
}

#define MAX_STRING_COMMAND 40

/* Called when a user program invokes the write() system call on the device */
static ssize_t spower_write(struct file *file, const char *user_buffer,
                            size_t len, loff_t *off)
{
	int ret=0;
	char command[MAX_STRING_COMMAND+1];
	struct usb_spower *dev=file->private_data;
	int cmd=0;
	int ms=0;

	if (len>MAX_STRING_COMMAND)
		return -ENOSPC;

	if (copy_from_user(command,user_buffer,len))
		return -EFAULT;

	command[len]='\0';

	if (strcmp(command,"on\n")==0 || strcmp(command,"off\n")==0)
		cmd=REQUEST_ONOFF;
	else if (strcmp(command,"start\n")==0 || strcmp(command,"stop\n")==0)
		cmd=REQUEST_STARTSTOP;
	else if (sscanf(command,"sampling_period %d",&ms)==1) {
		if (ms<50 || ms >4000)
			return -EINVAL;
		dev->sampling_period_ms=ms;
		return len;
	} else if (sscanf(command,"timer_period %d",&ms)==1) {
		if ((ret=spower_set_sampling_period(ms)))
			return ret;
		return len;
	} else
		return -EINVAL;

	if ((ret=send_packet_spower(dev,cmd))<0)
		return -ret;

	(*off)+=len;
	return len;

}

/* Custom version to parse float that uses 1000 as the default scale factor */
static int spower_parse_float(const char* str, int* val)
{
	int len=0;
	int unit=0;
	int dec=0;

	while (!isdigit(*str) && (*str)!='-') {
		str++;
		len++;
	}

	if (sscanf(str,"%d.%d",&unit,&dec)!=2)
		(*val)=-1;
	else
		(*val)=unit*1000+dec;

#ifdef DEBUG
	trace_printk("unit=%d,dec=%d\n",unit,dec);
#endif

	if (*str=='-') {
		/* Move forward 5 positions */
		/* -.--- */
		return len+5;
	} else {
		while (isdigit(*str) || (*str)=='.') {
			str++;
			len++;
		}
		return len;
	}
}




/*
	Extract data from the string provided by the device

	Sample input strings
	---------------------
	7 4.610V  0.439 A 2.019W  0.000Wh
	7 4.610V *0.439 A 2.019W  0.000Wh
	7 4.610V  -.--- A -.---W  -.---Wh

*/
static noinline int parse_spower_sample(const char* str, struct spower_sample* sample)
{
	if (strlen(str)<33) {
		sample->m_volts=sample->m_ampere=sample->m_watt=sample->m_watthour=-1;
		return 1;
	}

	str+=2; /* Ignore the first 2 characters */

	str+=spower_parse_float(str,&sample->m_volts);
	str+=spower_parse_float(str,&sample->m_ampere);
	str+=spower_parse_float(str,&sample->m_watt);
	str+=spower_parse_float(str,&sample->m_watthour);
	return 0;
}

static inline int spower_print_int(char* buf,int val,char* tag)
{

	if (val==-1)
		return sprintf(buf,"--%s",tag);
	else
		return sprintf(buf,"%d%s",val,tag);

}

static int get_summary_samples(unsigned long timestamp, struct spower_sample* sample)
{
	int nr_samples=0;
	struct spower_sample* cur;
	iterator_cbuffer_t it=get_iterator_cbuffer_t(spower_gbl.cbuffer,sizeof(struct spower_sample));

	memset(sample,0,sizeof(struct spower_sample));

	while ((cur=iterator_next_cbuffer_t(&it))) {
		if (cur->timestamp>=timestamp) {
			sample->m_volts+=cur->m_volts;
			sample->m_ampere+=cur->m_ampere;
			sample->m_watt+=cur->m_watt;
			sample->m_watthour+=cur->m_watthour;
			sample->m_ujoules+=cur->m_ujoules;
			nr_samples++;
		}
	}

	if (nr_samples>0) {
		sample->m_volts/=nr_samples;
		sample->m_ampere/=nr_samples;
		sample->m_watt/=nr_samples;
		sample->m_watthour/=nr_samples;
		/* Joules are cummulative, so no division here */
	}

	return nr_samples;
}


/* Get a sample that summarizes measurements collected from a given point */
int spower_get_sample(unsigned long from, struct spower_sample* sample)
{
	int nr_samples=0;
	unsigned long flags;
	read_lock_irqsave(&spower_gbl.lock,flags);
	nr_samples=get_summary_samples(from,sample);
	read_unlock_irqrestore(&spower_gbl.lock,flags);
	return nr_samples;
}


static ssize_t spower_read(struct file *file, char *user_buffer,
                           size_t len, loff_t *off)
{
	char kbuf[NR_BYTES_SPOWER_MSG+2]="";
	char* dest=kbuf;
	struct usb_spower *dev=file->private_data;
	struct spower_sample sample;
	unsigned long flags=0;
	int read_something=0;
	unsigned long timestamp=jiffies;

	mdelay(dev->sampling_period_ms);

	read_lock_irqsave(&spower_gbl.lock,flags);

#ifdef CONSUMER
	if (size_cbuffer_t(spower_gbl.cbuffer)>=sizeof(struct spower_sample)) {
		remove_items_cbuffer_t(spower_gbl.cbuffer,&sample,sizeof(struct spower_sample));
		read_something=1;
	}
#else
	read_something=get_summary_samples(timestamp,&sample);
	dest+=sprintf(dest,"samples=%d, ",read_something);

#endif
	read_unlock_irqrestore(&spower_gbl.lock,flags);

	if (read_something) {
		dest+=spower_print_int(dest,sample.m_volts,"mV, ");
		dest+=spower_print_int(dest,sample.m_ampere,"mA, ");
		dest+=spower_print_int(dest,sample.m_watt,"mW, ");
		dest+=spower_print_int(dest,sample.m_watthour,"mWh, ");
		dest+=spower_print_int(dest,sample.m_ujoules,"uJ, ");
		dest+=sprintf(dest,"%lluuJ\n",spower_gbl.cummulative_energy);
	} else {
		dest+=sprintf(dest,"Nothing to read\n");
	}

	if (copy_to_user(user_buffer,kbuf,dest-kbuf))
		return -EFAULT;

	return dest-kbuf;
}

/*
 * Operations associated with the character device
 * exposed by driver
 *
 */
static const pmctrack_proc_ops_t spower_fops = {
	.PMCT_PROC_WRITE =	spower_write,	 	/* write() operation on the file */
	.PMCT_PROC_READ =		spower_read,
	.PMCT_PROC_OPEN =		spower_open,			/* open() operation on the file */
	.PMCT_PROC_RELEASE =	spower_release, 		/* close() operation on the file */
	.PMCT_PROC_LSEEK = default_llseek
};

/*
 * Return permissions and pattern enabling udev
 * to create device file names under /dev
 *
 * For each spower connected device a character device file
 * named /dev/usb/spower<N> will be created automatically
 */
char* set_device_permissions(struct device *dev, umode_t *mode)
{
	if (mode)
		(*mode)=0666; /* RW permissions */
	return kasprintf(GFP_KERNEL, "usb/%s", dev_name(dev)); /* Return formatted string */
}


/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver spower_class = {
	.name =		"spower%d",  /* Pattern used to create device files */
	.devnode=	set_device_permissions,
	.fops =		&spower_fops,
	.minor_base =	USB_SPOWER_MINOR_BASE,
};

/*
 * Invoked when the USB core detects a new
 * spower device connected to the system.
 */
static int spower_probe(struct usb_interface *interface,
                        const struct usb_device_id *id)
{
	struct usb_spower *dev;
	int retval = -ENOMEM;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i, int_end_size;
	unsigned long flags;

	/*
	 * Allocate memory for a usb_spower structure.
	 * This structure represents the device state.
	 * The driver assigns a separate structure to each spower device
	 *
	 */
	dev = kzalloc(sizeof(struct usb_spower),GFP_KERNEL);

	if (!dev) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error;
	}

	/* Initialize the various fields in the usb_spower structure */
	kref_init(&dev->kref);
	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	dev->sampling_period_ms=SPOWER_DEFAULT_SAMPLING_PERIOD;
	spin_lock_init(&dev->lock);


	iface_desc = interface->cur_altsetting;

	/* Set up interrupt endpoint information. */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
		     == USB_DIR_IN)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		        == USB_ENDPOINT_XFER_INT))
			dev->int_in_endpoint = endpoint;

		if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
		     == USB_DIR_OUT)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		        == USB_ENDPOINT_XFER_INT))
			dev->int_out_endpoint = endpoint;
	}

	if (! dev->int_in_endpoint) {
		pr_err("could not find interrupt in endpoint");
		goto error;
	}

	if (! dev->int_out_endpoint) {
		pr_err("could not find interrupt out endpoint");
		goto error;
	}

	int_end_size = le16_to_cpu(dev->int_in_endpoint->wMaxPacketSize);

	dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (! dev->int_in_urb) {
		retval = -ENOMEM;
		goto error;
	}

	dev->int_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (! dev->int_out_urb) {
		retval = -ENOMEM;
		goto error;
	}

	memset(dev->int_out_buffer,0,NR_BYTES_SPOWER_MSG);
	dev->int_out_buffer[0]=REQUEST_DATA;

	/* Initialize interrupt URB. */
	usb_fill_int_urb(dev->int_out_urb, dev->udev,
	                 usb_sndintpipe(dev->udev,
	                                dev->int_out_endpoint->bEndpointAddress),
	                 dev->int_out_buffer,
	                 NR_BYTES_SPOWER_MSG,
	                 spower_int_out_callback,
	                 dev,
	                 dev->int_out_endpoint->bInterval);


	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &spower_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev,
		        "Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* Update pointer to device (If it was unused before) */
	write_lock_irqsave(&spower_gbl.lock,flags);

	if (!spower_gbl.dev)
		spower_gbl.dev=dev;

	write_unlock_irqrestore(&spower_gbl.lock,flags);


	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
	         "SmartPower device now attached to spower-%d",
	         interface->minor);
	return 0;

error:
	/* Free up urbs */
	if (dev->int_in_urb)
		usb_free_urb(dev->int_in_urb);

	if (dev->int_out_urb)
		usb_free_urb(dev->int_out_urb);

	if (dev)
		/* this frees up allocated memory */
		kref_put(&dev->kref, spower_delete);
	return retval;
}

/*
 * Invoked when a spower device is
 * disconnected from the system.
 */
static void spower_disconnect(struct usb_interface *interface)
{
	struct usb_spower *dev;
	int minor = interface->minor;
	unsigned long flags;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* Free up urbs */
	if (dev->int_in_urb)
		usb_free_urb(dev->int_in_urb);

	if (dev->int_out_urb)
		usb_free_urb(dev->int_out_urb);

	/* Remove pointer from device */
	write_lock_irqsave(&spower_gbl.lock,flags);

	if (spower_gbl.dev==dev)
		spower_gbl.dev=NULL;

	write_unlock_irqrestore(&spower_gbl.lock,flags);

	/* give back our minor */
	usb_deregister_dev(interface, &spower_class);

	/* prevent more I/O from starting */
	dev->interface = NULL;

	/* decrement our usage count */
	kref_put(&dev->kref, spower_delete);

	dev_info(&interface->dev, "SmartPower device #%d has been disconnected", minor);
}

/* Define these values to match your devices */
#define SPOWER_VENDOR_ID	0X04d8
#define SPOWER_PRODUCT_ID	0X003f

/* table of devices that work with this driver */
static const struct usb_device_id spower_table[] = {
	{ USB_DEVICE(SPOWER_VENDOR_ID,  SPOWER_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, spower_table);

static struct usb_driver spower_driver = {
	.name =		"spower",
	.probe =	spower_probe,
	.disconnect =	spower_disconnect,
	.id_table =	spower_table,
};

/* Module initialization */
int spower_register_driver(void)
{
	int ret=0;

	spower_gbl.dev=NULL; /* Set to NULL initially */
	spower_gbl.cbuffer=create_cbuffer_t(CBUFFER_CAPACITY*sizeof(struct spower_sample));

	if (!spower_gbl.cbuffer)
		return -ENOMEM;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
	/* Timer initialization */
	init_timer(&spower_gbl.timer);
	spower_gbl.timer.data=(unsigned long)&spower_gbl;
	spower_gbl.timer.function=spower_fire_timer;
	spower_gbl.timer.expires=jiffies;  /* It does not matter for now */
#else
	timer_setup(&spower_gbl.timer, spower_fire_timer, 0);
#endif
	spower_gbl.timer_period=(HZ*SPOWER_DEFAULT_TIMER_PERIOD)/1000;
	spower_gbl.started=0;
	spower_gbl.cummulative_energy=0;
	rwlock_init(&spower_gbl.lock);

	ret=usb_register(&spower_driver);

	if (ret)
		destroy_cbuffer_t(spower_gbl.cbuffer);

	return ret;
}

/* Module cleanup function */
void spower_unregister_driver(void)
{
	usb_deregister(&spower_driver);
	destroy_cbuffer_t(spower_gbl.cbuffer);
}

