/*
 *  vexpress_sensors_mm.c
 *
 * 	PMCTrack monitoring module enabling to measure energy consumption 
 *  on the ARM Coretile Express TC2 board and the ARM Juno development board
 * 
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *	              and Javier Setoain <jsetoain@ucm.es>      
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/monitoring_mod.h>

#define ARM_VERSATILE_SENSORS_STR "ARM vexpress sensors"

#ifdef CONFIG_PMC_ARM64
/* three energy registers (ARM Juno board) */
enum versatile_sensors {VEXPRESS_ENERGY_BIG,VEXPRESS_ENERGY_LITTLE,VEXPRESS_ENERGY_SYS,VEXPRESS_NR_SENSORS};
#else
/* two energy registers (ARM Coretile Express TC2 board) */
enum versatile_sensors {VEXPRESS_ENERGY_BIG,VEXPRESS_ENERGY_LITTLE,VEXPRESS_NR_SENSORS};
#endif

/* Per-thread private data for this monitoring module */
typedef struct {
	uint64_t cur_sensor_value[VEXPRESS_NR_SENSORS];
	uint64_t acum_sensor[VEXPRESS_NR_SENSORS];
	int first_time;
	uint_t security_id;
} vexpress_sensors_thread_data_t;

/* Initialize resources to support system-wide monitoring */
static int initialize_system_wide_sensor_structures(void);

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
typedef struct {
	unsigned int offset_high;
	unsigned int offset_low;
	void* address_high;
	void* address_low;
	unsigned char big_cluster;
	u64 scale_factor;		/* Factor to convert to microJoules*/
} vexpress_sensor_t;

/* Array of energy meter registers */
vexpress_sensor_t energy_sensors[VEXPRESS_NR_SENSORS]= {
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
int read_energy_sensor(vexpress_sensor_t* sensor, uint64_t* value)
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

/* MM initialization */
static int vexpress_sensors_enable_module(void)
{
	uint64_t values[VEXPRESS_NR_SENSORS];
	int retval;
	int i;

	if(request_mem_region(APB_REGISTERS_BASE_ADDRESS, 0x1000,
	                      ARM_VERSATILE_SENSORS_STR))
		apbregs_base_ptr = ioremap(APB_REGISTERS_BASE_ADDRESS, 0x1000);
	if(!apbregs_base_ptr) {
		printk(KERN_ERR "Couldn't obtain APB registers virtual address\n");
		return -EINVAL;
	}

	if ((retval=initialize_system_wide_sensor_structures())) {
		printk(KERN_INFO "Couldn't initialize system-wide sensor structures");
		return retval;
	}

	for (i = 0; i < VEXPRESS_NR_SENSORS; i++) {
		energy_sensors[i].address_high =
		    apbregs_base_ptr + energy_sensors[i].offset_high;
		energy_sensors[i].address_low =
		    apbregs_base_ptr + energy_sensors[i].offset_low;
		read_energy_sensor(&energy_sensors[i],&values[i]);
		printk(KERN_INFO "sensor[%i]=%llu\n",i,values[i]);
	}

	printk(KERN_ALERT "%s module loaded!!\n",ARM_VERSATILE_SENSORS_STR);
	return 0;
}

/* MM cleanup function */
static void vexpress_sensors_disable_module(void)
{
	if(apbregs_base_ptr) {
		int i;
		iounmap(apbregs_base_ptr);
		apbregs_base_ptr = NULL;
		for (i = 0; i < VEXPRESS_NR_SENSORS; i++) {
			energy_sensors[i].address_high = NULL;
			energy_sensors[i].address_low = NULL;
		}
		release_mem_region(APB_REGISTERS_BASE_ADDRESS, 0x1000);
	}
	printk(KERN_ALERT "%s module unloaded!!\n",ARM_VERSATILE_SENSORS_STR);
}


#else
/** Code path for the ARM Coretile Express TC2 board **/

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/vexpress.h>

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
typedef struct {
	struct device_attribute* da;
	struct device* dev;
	struct regmap* reg;
	unsigned char big_cluster;
} vexpress_sensor_t;

/* Array of energy sensors */
vexpress_sensor_t energy_sensors[VEXPRESS_NR_SENSORS];

/* Low-level code to retrieve energy readings */
int read_energy_sensor(vexpress_sensor_t* sensor, uint64_t* value)
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

/* MM initialization */
static int vexpress_sensors_enable_module(void)
{
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


	if ((retval=initialize_system_wide_sensor_structures())) {
		printk(KERN_INFO "Couldn't initialize system-wide sensor structures");
		return retval;
	}

	printk(KERN_ALERT "%s module loaded!!\n",ARM_VERSATILE_SENSORS_STR);
	return 0;
}

/* MM cleanup function */
static void vexpress_sensors_disable_module(void)
{
	int i=0;

	/* Decrease hwmon's object ref count */
	for (i=0; i<VEXPRESS_NR_SENSORS; i++)
		if (energy_sensors[i].dev)
			pmctrack_hwmon_put_device(energy_sensors[i].dev);

	printk(KERN_ALERT "%s module unloaded!!\n",ARM_VERSATILE_SENSORS_STR);
}

#endif

/* Return the capabilities/properties of this monitoring module */
static void vexpress_sensors_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	usage->hwpmc_mask=0;
	usage->nr_virtual_counters=VEXPRESS_NR_SENSORS;
	usage->nr_experiments=0; 
	usage->vcounter_desc[0]="energy_big_cluster";
	usage->vcounter_desc[1]="energy_little_cluster";
#ifdef CONFIG_PMC_ARM64
	usage->vcounter_desc[2]="energy_sys";
#endif

}

static int vexpress_sensors_on_read_config(char* str, unsigned int len)
{
	return 0;
}

static int vexpress_sensors_on_write_config(const char *str, unsigned int len)
{
	return 0;
}

/* on fork() callback */
static int vexpress_sensors_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{
	int i=0;
	vexpress_sensors_thread_data_t* data=NULL;

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;

	data= kmalloc(sizeof (vexpress_sensors_thread_data_t), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->security_id=current_monitoring_module_security_id();
	data->first_time=1;
	for (i=0; i<VEXPRESS_NR_SENSORS; i++) {
		data->cur_sensor_value[i]=0;
		data->acum_sensor[i]=0;
	}

	prof->monitoring_mod_priv_data = data;
	return 0;
}

/* on exec() callback */
static void vexpress_sensors_on_exec(pmon_prof_t* prof) { }

/* 
 * Read energy registers/sensors and update cumulative counters in the 
 * private thread structure.
 */
static inline void do_read_sensors(vexpress_sensors_thread_data_t* tdata, int acum)
{
	int i=0;
	uint64_t old_value, delta;

	if (!acum) {
		/* Read energy count from registers */
		for (i=0; i<VEXPRESS_NR_SENSORS; i++)
			read_energy_sensor(&energy_sensors[i], &tdata->cur_sensor_value[i]);
	} else {
		for (i=0; i<VEXPRESS_NR_SENSORS; i++) {
			/* Save old value */
			old_value=tdata->cur_sensor_value[i];
			read_energy_sensor(&energy_sensors[i], &tdata->cur_sensor_value[i]);

#ifdef DEBUG
			if (i==0)
				printk(KERN_ALERT "0x%x \n",tdata->cur_sensor_value[i]);
#endif
			delta=tdata->cur_sensor_value[i]-old_value;

			tdata->acum_sensor[i]+=delta;
		}
	}
}


/* 
 * Update cumulative energy counters in the thread structure and
 * set the associated virtual counts in the PMC sample structure
 */
static int vexpress_sensors_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data)
{
	vexpress_sensors_thread_data_t* tdata=prof->monitoring_mod_priv_data;
	int i=0;
	int cnt_virt=0;

#ifdef CONFIG_PMC_ARM
	/* Avoid reading energy counters from process context (it crashes when doing so) */
	if (flags & MM_EXIT)
		return 0;
#endif

	if (tdata!=NULL) {

		do_read_sensors(tdata,1);

		/* Embed virtual counter information so that the user can see what's going on */

		for (i=0; i<VEXPRESS_NR_SENSORS; i++) {
			if ((prof->virt_counter_mask & (1<<i)) ) { 
				sample->virt_mask|=(1<<i);
				sample->nr_virt_counts++;
				sample->virtual_counts[cnt_virt]=tdata->acum_sensor[i];
				cnt_virt++;
			}
			/* Reset no matter what */
			tdata->acum_sensor[i]=0;
		}
	}

	return 0;
}

/* Free up private data */
static void vexpress_sensors_on_free_task(pmon_prof_t* prof)
{
	if (prof->monitoring_mod_priv_data)
		kfree(prof->monitoring_mod_priv_data);
}

/* on switch_in callback */
void vexpress_sensors_on_switch_in(pmon_prof_t* prof)
{
	vexpress_sensors_thread_data_t* data=(vexpress_sensors_thread_data_t*)prof->monitoring_mod_priv_data;

	if (!data || data->security_id!=current_monitoring_module_security_id() )
		return;

	/* Update prev counts */
	do_read_sensors(data,0);

	if (data->first_time)
		data->first_time=0;
}

/* on switch_out callback */
void vexpress_sensors_on_switch_out(pmon_prof_t* prof)
{
	vexpress_sensors_thread_data_t* data=(vexpress_sensors_thread_data_t*)prof->monitoring_mod_priv_data;

	if (!data || data->security_id!=current_monitoring_module_security_id())
		return;

	/* Accumulate energy readings */
	if (!data->first_time)
		do_read_sensors(data,1);
}

/* Modify this function if necessary to expose energy readings to the OS scheduler */
static int vexpress_sensors_get_current_metric_value(pmon_prof_t* prof, int key, uint64_t* value)
{
	return -1;
}

/* Support for system-wide power measurement (Reuse per-thread info as is) */
static DEFINE_PER_CPU(vexpress_sensors_thread_data_t, cpu_syswide);

/* Initialize resources to support system-wide monitoring on ARM boards */
static int initialize_system_wide_sensor_structures(void)
{
	int cpu,i;
	vexpress_sensors_thread_data_t* data;

	for_each_possible_cpu(cpu) {
		data=&per_cpu(cpu_syswide, cpu);

		for (i=0; i<VEXPRESS_NR_SENSORS; i++) {
			data->cur_sensor_value[i]=0;
			data->acum_sensor[i]=0;
		}

		/* These two fields are not used by the system-wide monitor
			Nevertheless we initialize them both just in case...
		*/
		data->security_id=current_monitoring_module_security_id();
		data->first_time=1;
	}

	return 0;
}

/*	Invoked on each CPU when starting up system-wide monitoring mode */
static int vexpress_sensors_on_syswide_start_monitor(int cpu, unsigned int virtual_mask)
{
	vexpress_sensors_thread_data_t* data;
	int i=0;

	/* Probe only */
	if (cpu==-1) {
		/* Make sure virtual_mask only has 1s in the right bits
			- This can be checked easily
				and( virtual_mask,not(2^nr_available_vcounts - 1)) == 0
		*/
		if (virtual_mask & ~((1<<VEXPRESS_NR_SENSORS)-1))
			return -EINVAL;
		else
			return 0;
	}

	data=&per_cpu(cpu_syswide, cpu);

	for (i=0; i<VEXPRESS_NR_SENSORS; i++) {
		data->cur_sensor_value[i]=0;
		data->acum_sensor[i]=0;
	}


	/* Update prev counts */
	do_read_sensors(data,0);

	return 0;
}

/*	Invoked on each CPU when stopping system-wide monitoring mode */
static void vexpress_sensors_on_syswide_refresh_monitor(int cpu, unsigned int virtual_mask)
{
	vexpress_sensors_thread_data_t* data=&per_cpu(cpu_syswide, cpu);

	/* Accumulate energy readings */
	do_read_sensors(data,1);
}

/* 	Dump virtual-counter values for this CPU */
static void vexpress_sensors_on_syswide_dump_virtual_counters(int cpu, unsigned int virtual_mask,pmc_sample_t* sample)
{
	vexpress_sensors_thread_data_t* data=&per_cpu(cpu_syswide, cpu);
	int i=0;
	int cnt_virt=0;

	/* Embed virtual counter information so that the user can see what's going on */
	for (i=0; i<VEXPRESS_NR_SENSORS; i++) {
		if ((virtual_mask & (1<<i)) ) {
			sample->virt_mask|=(1<<i);
			sample->nr_virt_counts++;
			sample->virtual_counts[cnt_virt]=data->acum_sensor[i];
			cnt_virt++;
		}
		/* Reset no matter what */
		data->acum_sensor[i]=0;
	}
}

/* Implementation of the monitoring_module_t interface */
monitoring_module_t vexpress_sensors_mm = {
	.info=ARM_VERSATILE_SENSORS_STR,
	.id=-1,
	.enable_module=vexpress_sensors_enable_module,
	.disable_module=vexpress_sensors_disable_module,
	.on_read_config=vexpress_sensors_on_read_config,
	.on_write_config=vexpress_sensors_on_write_config,
	.on_fork=vexpress_sensors_on_fork,
	.on_exec=vexpress_sensors_on_exec,
	.on_new_sample=vexpress_sensors_on_new_sample,
	.on_free_task=vexpress_sensors_on_free_task,
	.on_switch_in=vexpress_sensors_on_switch_in,
	.on_switch_out=vexpress_sensors_on_switch_out,
	.get_current_metric_value=vexpress_sensors_get_current_metric_value,
	.module_counter_usage=vexpress_sensors_module_counter_usage,
	.on_syswide_start_monitor=vexpress_sensors_on_syswide_start_monitor,
	.on_syswide_refresh_monitor=vexpress_sensors_on_syswide_refresh_monitor,
	.on_syswide_dump_virtual_counters=vexpress_sensors_on_syswide_dump_virtual_counters
};
