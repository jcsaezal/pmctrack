/*
 *  vexpress_sensors_core.c
 *
 * 	Implementation of the high-level API enabling to measure energy consumption 
 *  on the ARM Coretile Express TC2 board and the ARM Juno development board
 * 
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *	              and Javier Setoain <jsetoain@ucm.es>      
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/vexpress_sensors.h>
#include <linux/ioport.h>
#include <linux/regmap.h>
#include <asm/io.h>

#ifdef CONFIG_PMC_ARM64
/* three energy registers (ARM Juno board) */
enum versatile_sensors {VEXPRESS_ENERGY_BIG,VEXPRESS_ENERGY_LITTLE,VEXPRESS_ENERGY_SYS,VEXPRESS_NR_SENSORS};
#else
/* two energy registers (ARM Coretile Express TC2 board) */
enum versatile_sensors {VEXPRESS_ENERGY_BIG,VEXPRESS_ENERGY_LITTLE,VEXPRESS_NR_SENSORS};
#endif


#ifdef CONFIG_PMC_ARM64
/** Code path for the ARM Juno board **/

/*
 * Table 4-19 V2M-Juno motherboard SCC register summary
 * 4 Programmers Model 4.5 APB energy meter registers
 * pp 102-103
 */

#define APB_REGISTERS_BASE_ADDRESS		0x1c010000
#define ADDR_SYS_ENM_CH1_L_A57_OFFSET	0x108
#define ADDR_SYS_ENM_CH1_H_A57_OFFSET	0x10c
#define ADDR_SYS_ENM_CH1_L_A53_OFFSET	0x110
#define ADDR_SYS_ENM_CH1_H_A53_OFFSET	0x114
#define ADDR_SYS_ENM_L_SYS_OFFSET		0x100
#define ADDR_SYS_ENM_H_SYS_OFFSET		0x104

/* Structure describing an energy meter register */
struct vexpress_sensor{
	unsigned int offset_high;
	unsigned int offset_low;
	void* address_high;
	void* address_low;
	unsigned char big_cluster;
	u64 scale_factor;		/* Factor to convert to microJoules*/
} vexpress_sensor;

/* Array of energy meter registers */
static vexpress_sensor_t energy_sensors[VEXPRESS_NR_SENSORS]= {
	{
		.offset_high=ADDR_SYS_ENM_CH1_H_A57_OFFSET,
		.offset_low=ADDR_SYS_ENM_CH1_L_A57_OFFSET,
		.address_high=NULL,
		.address_low=NULL,
		.big_cluster=1,
		.scale_factor=6174,
	},
	{
		.offset_high=ADDR_SYS_ENM_CH1_H_A53_OFFSET,
		.offset_low=ADDR_SYS_ENM_CH1_L_A53_OFFSET,
		.address_high=NULL,
		.address_low=NULL,
		.big_cluster=0,
		.scale_factor=12348,
	},
	{
		.offset_high=ADDR_SYS_ENM_H_SYS_OFFSET,
		.offset_low=ADDR_SYS_ENM_L_SYS_OFFSET,
		.address_high=NULL,
		.address_low=NULL,
		.big_cluster=0,
		.scale_factor=12348,
	},
};

/* Low-level code to retrieve energy readings */
int raw_read_energy_sensor(vexpress_sensor_t* sensor, uint64_t* value)
{
	uint64_t sensorval;
	sensorval = ( ((u64)ioread32(sensor->address_high) << 32) |
	              (u64)ioread32(sensor->address_low) );

	// XXX convert to microjoules
	*value = sensorval / sensor->scale_factor;

	return 0;
}

/* 
 * Pointer to store the base virtual address  
 * used to map the physical addresses of the APB energy registers
 */
static void* apbregs_base_ptr = NULL;

/* Initialize sensor connection */
int initialize_energy_sensors(vexpress_sensor_t* sensor_descriptors[],
							  const char* description[],
							  int* nr_sensors){
	int i;

	if(request_mem_region(APB_REGISTERS_BASE_ADDRESS, 0x1000,
	                      "ARM_VERSATILE_SENSORS"))
		apbregs_base_ptr = ioremap(APB_REGISTERS_BASE_ADDRESS, 0x1000);

	if(!apbregs_base_ptr) {
		printk(KERN_ERR "Couldn't obtain APB registers virtual address\n");
		return -EINVAL;
	}

	for (i=0;i<VEXPRESS_NR_SENSORS;i++) {
		energy_sensors[i].address_high =
            apbregs_base_ptr + energy_sensors[i].offset_high;
        energy_sensors[i].address_low =
            apbregs_base_ptr + energy_sensors[i].offset_low;
		sensor_descriptors[i]=&energy_sensors[i];
	}

	description[0]="energy_big";
	description[1]="energy_little";
	description[2]="energy_sys";
	(*nr_sensors)=VEXPRESS_NR_SENSORS;
	return 0;
}

/* Free up resources associated with sensors */
int release_energy_sensors(vexpress_sensor_t* sensor_descriptors[],
							  int nr_sensors)
{
	if(apbregs_base_ptr) {
		int i;
		iounmap(apbregs_base_ptr);
		apbregs_base_ptr = NULL;
		for (i = 0; i < nr_sensors; i++) {
			sensor_descriptors[i]->address_high = NULL;
			sensor_descriptors[i]->address_low = NULL;
		}
		release_mem_region(APB_REGISTERS_BASE_ADDRESS, 0x1000);
	}
	return 0;
}


#else
/** Code path for the ARM Coretile Express TC2 board **/

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/vexpress.h>
#include <linux/pmctrack.h>

/* "Pasted" necessary structures for the HWMON-PMCTrack bridge to work */

struct hwmon_device {
	const char *name;
	struct device dev;
};
#define to_hwmon_device(d) container_of(d, struct hwmon_device, dev)
#define to_device_attr(attrib) container_of(attrib, struct device_attribute, attr)

/* 
 * Structures found in the drivers/hwmon/vexpress.c
 * (HWmon driver for vexpress sensors)
 */
struct vexpress_hwmon_data {
	struct device *hwmon_dev;
	struct regmap *reg;
};

struct vexpress_hwmon_type {
	const char *name;
	const struct attribute_group **attr_groups;
};

/* Structure describing an energy sensor in the TC2 board */
struct vexpress_sensor {
	struct device_attribute* da;
	struct device* dev;
	struct regmap* reg;
	unsigned char big_cluster;
} ;

/* Array of energy sensors */
vexpress_sensor_t energy_sensors[VEXPRESS_NR_SENSORS];

/* Low-level code to retrieve energy readings */
int raw_read_energy_sensor(vexpress_sensor_t* sensor, uint64_t* value)
{

	int err;
	u32 value_hi, value_lo;

	if (!sensor->reg)
		return -EINVAL;

	err = regmap_read(sensor->reg, 0, &value_lo);
	if (err) {
		(*value)=0;
		return err;
	}

	err = regmap_read(sensor->reg, 1, &value_hi);
	if (err) {
		(*value)=0;
		return err;
	}

	(*value)=( ((u64)value_hi << 32) | (u64) value_lo);

	return 0;
}

/* Get a reference to hwmon devices associated with energy sensors */
static int find_energy_sensors(void)
{
	const char* available_sns[20];
	int nr_sensors=0;
	int i=0,j=0;
	struct device* dev;
	struct hwmon_device* hwmon_dev;
	struct vexpress_hwmon_data* hwmon_data;
	int nr_sensors_found=0;
	vexpress_sensor_t* curr;
	char buf[50];
	const struct attribute_group *group;

	pmctrack_hwmon_list_devices(20,available_sns,&nr_sensors);

	printk(KERN_INFO "Hwmon devices found: %d\n", nr_sensors);
	for (i=0; i<nr_sensors; i++) {
		/* Retrieve device */
		dev=pmctrack_hwmon_get_device(available_sns[i]);

		if (!dev)
			continue;

		hwmon_dev=to_hwmon_device(dev);

		printk(KERN_INFO "%s (%s) \n", available_sns[i], hwmon_dev->name);

		/* Deal only with vexpress_energy sensors */
		if (strcmp(hwmon_dev->name,"vexpress_energy")!=0)
			goto put_device_now;

		hwmon_data = dev_get_drvdata(dev);

		if (!hwmon_data) {
			printk(KERN_INFO "NULL dev data\n");
			goto put_device_now;
		}

		/* We found a sensor, Now it's time to initialize the structure... */
		curr=&energy_sensors[nr_sensors_found++];
		curr->reg=hwmon_data->reg;
		curr->dev=dev;
		curr->da=NULL;
		curr->big_cluster=0; /* Assume little for now */

		if (!dev->groups || !dev->groups[0]) {
			printk(KERN_INFO "NULL groups\n");
			goto put_device_now;
		}

		group=dev->groups[0];

		/* Traverse attributes and check value */
		for (j = 0; group->attrs[j]; j++) {
			struct device_attribute* da=to_device_attr(group->attrs[j]);
			const char* name=group->attrs[j]->name;
			int len=strlen(name);

			if (len>6 && strcmp("_input",name+(len-6))==0) {
				/* Assign Input attibute if  found */
				curr->da=da;
				da->show(dev,da,buf);
				printk(KERN_INFO "\t%s=%s",group->attrs[j]->name,buf);
			} else if (len>6 && strcmp("_label",name+(len-6))==0) {
				da->show(dev,da,buf);

				/* Detect Big core sensor */
				if (strncmp(buf,"A15",3)==0) {
					curr->big_cluster=1;
				}

				printk(KERN_INFO "\t%s=%s (Big cluster: %u)\n",group->attrs[j]->name,buf,curr->big_cluster);
			}
		}

		continue;	/* Do not decrease ref count since we're using the sensor .... */
put_device_now:
		/* Decrease ref count */
		pmctrack_hwmon_put_device(dev);
	}

	return nr_sensors_found;
}

/* Initialize sensor connection */
int initialize_energy_sensors(vexpress_sensor_t* sensor_descriptors[],
							  const char* description[],
							  int* nr_sensors){
	int nr_sensors_detected;
	int i=0;
	int retval=0;

	memset(energy_sensors,0,sizeof(vexpress_sensor_t)*VEXPRESS_NR_SENSORS);

	nr_sensors_detected=find_energy_sensors();

	if (nr_sensors_detected<VEXPRESS_NR_SENSORS) {
		printk(KERN_ALERT "Can't detect all the available sensors!!\n");

		for (i=0; i<nr_sensors_detected; i++) {
			pmctrack_hwmon_put_device(energy_sensors[i].dev);
			energy_sensors[i].dev=NULL;
		}
		return -EINVAL;
	}


	for (i=0;i<VEXPRESS_NR_SENSORS;i++)
		sensor_descriptors[i]=&energy_sensors[i];

	description[0]="energy_big";
	description[1]="energy_little";
	(*nr_sensors)=VEXPRESS_NR_SENSORS;

	return 0;
}

/* Free up resources associated with sensors */
int release_energy_sensors(vexpress_sensor_t* sensor_descriptors[],
							  int nr_sensors)
{
	int i=0;

	/* Decrease hwmon's object ref count */
	for (i=0; i<nr_sensors; i++)
		if (sensor_descriptors[i]->dev)
			pmctrack_hwmon_put_device(sensor_descriptors[i]->dev);

	return 0;
}

#endif

/* 
 * Retrieve energy consumption readings from the sensors.
 * The reading is provided with respect to the last time 
 * sensors were checked (difference only).
 */
void do_read_energy_sensors(vexpress_sensor_t* sensor_descriptors[],
							vexpress_sensors_count_t* sensor_counts,
							int nr_sensors,
						 	int acum){
	int i=0;
	uint64_t delta;

	if (!acum) {
		/* Read energy count from registers */
		for (i=0; i<nr_sensors; i++)
			raw_read_energy_sensor(sensor_descriptors[i], &sensor_counts[i].cur);
	} else {
		for (i=0; i<nr_sensors; i++) {
			/* Save old value */
			sensor_counts[i].prev=sensor_counts[i].cur;
			raw_read_energy_sensor(sensor_descriptors[i], &sensor_counts[i].cur);

#ifdef DEBUG
			if (i==0)
				printk(KERN_ALERT "0x%x \n",sensor_counts[i].cur_sensor_value);
#endif
			delta=sensor_counts[i].cur-sensor_counts[i].prev;

			sensor_counts[i].acum+=delta;
		}
	}
}
