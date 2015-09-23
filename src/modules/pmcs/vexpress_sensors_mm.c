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
#include <pmc/vexpress_sensors.h>

#define ARM_VERSATILE_SENSORS_STR "ARM vexpress sensors"


/* Per-thread private data for this monitoring module */
typedef struct {
    vexpress_sensors_count_t sensor_counts[MAX_VEXPRESS_ENERGY_SENSORS];
    int first_time;
    uint_t security_id;
} vexpress_sensors_thread_data_t;


/* Initialize resources to support system-wide monitoring */
static int initialize_system_wide_sensor_structures(void);
static vexpress_sensor_t* sensors_desc[MAX_VEXPRESS_ENERGY_SENSORS];
static const char* sensor_information[MAX_VEXPRESS_ENERGY_SENSORS];
static int nr_sensors_available=0;

/* MM initialization */
static int vexpress_sensors_enable_module(void)
{
	uint64_t value;
	int retval;
	int i;

	nr_sensors_available=0;
	/* Initialize pointer vector */
	for (i=0;i<MAX_VEXPRESS_ENERGY_SENSORS;i++){
		sensors_desc[i]=NULL;
		sensor_information[i]=NULL;
	}

	if((retval=initialize_energy_sensors(sensors_desc,sensor_information,&nr_sensors_available))){
		printk(KERN_INFO "Couldn't initialize energy sensors");
		return -EINVAL;
	}

	if ((retval=initialize_system_wide_sensor_structures())) {
		printk(KERN_INFO "Couldn't initialize system-wide sensor structures");
		return retval;
	}

	for (i = 0; i < nr_sensors_available; i++) {
		raw_read_energy_sensor(sensors_desc[i],&value);
		printk(KERN_INFO "sensor[%i]=%llu\n",i,value);
	}

	printk(KERN_ALERT "%s module loaded!!\n",ARM_VERSATILE_SENSORS_STR);
	return 0;
}

/* MM cleanup function */
static void vexpress_sensors_disable_module(void)
{
	release_energy_sensors(sensors_desc,nr_sensors_available);
	printk(KERN_ALERT "%s module unloaded!!\n",ARM_VERSATILE_SENSORS_STR);
}



/* Return the capabilities/properties of this monitoring module */
static void vexpress_sensors_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	int i=0;
	usage->hwpmc_mask=0;
	usage->nr_virtual_counters=nr_sensors_available;
	usage->nr_experiments=0; 

	for (i=0;i<nr_sensors_available;i++)
		usage->vcounter_desc[i]=sensor_information[i];
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
	for (i=0; i<nr_sensors_available; i++) 
		initialize_vexpress_sensors_count(&data->sensor_counts[i]);

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
	 do_read_energy_sensors(sensors_desc,tdata->sensor_counts,
							nr_sensors_available,acum);
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

		for (i=0; i<nr_sensors_available; i++) {
			if ((prof->virt_counter_mask & (1<<i)) ) { 
				sample->virt_mask|=(1<<i);
				sample->nr_virt_counts++;
				sample->virtual_counts[cnt_virt]=tdata->sensor_counts[i].acum;
				cnt_virt++;
			}
			/* Reset no matter what */
			tdata->sensor_counts[i].acum=0;
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

		for (i=0; i<nr_sensors_available; i++) 
			initialize_vexpress_sensors_count(&data->sensor_counts[i]);

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
		if (virtual_mask & ~((1<<nr_sensors_available)-1))
			return -EINVAL;
		else
			return 0;
	}

	data=&per_cpu(cpu_syswide, cpu);

	for (i=0; i<nr_sensors_available; i++)
		initialize_vexpress_sensors_count(&data->sensor_counts[i]);

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
	for (i=0; i<nr_sensors_available; i++) {
		if ((virtual_mask & (1<<i)) ) {
			sample->virt_mask|=(1<<i);
			sample->nr_virt_counts++;
			sample->virtual_counts[cnt_virt]=data->sensor_counts[i].acum;
			cnt_virt++;
		}
		/* Reset no matter what */
		data->sensor_counts[i].acum=0;
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
