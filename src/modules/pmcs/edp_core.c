#include <pmc/edp.h>

#if defined(CONFIG_PMC_ARM) ||   defined(CONFIG_PMC_ARM) 
#include <pmc/vexpress_sensors.h>

#ifndef AMP_CORE_TYPES
#define AMP_CORE_TYPES 2
#endif
#ifndef AMP_FAST_CORE
#define AMP_FAST_CORE 1
#endif
#ifndef AMP_SLOW_CORE
#define AMP_SLOW_CORE 0
#endif

typedef struct {
	vexpress_sensors_count_t sensor_values[MAX_VEXPRESS_ENERGY_SENSORS];
	uint64_t glb_instr_counter[AMP_CORE_TYPES];
	volatile int edp_counts_on;
	spinlock_t lock;
} edpmm_global_data_t;

static edpmm_global_data_t edpmm_global_counters;
static vexpress_sensor_t* energy_sensors[MAX_VEXPRESS_ENERGY_SENSORS];
static int nr_sensors_available=0;
static const char* energy_sensor_descr[MAX_VEXPRESS_ENERGY_SENSORS];

int edp_initialize_environment(void)
{
	int i=0;
	int retval=0;

	if((retval=initialize_energy_sensors(energy_sensors,
	                                     energy_sensor_descr,
	                                     &nr_sensors_available))) {
		printk(KERN_INFO "Couldn't initialize energy sensors");
		return -EINVAL;
	}

	for (i=0; i<nr_sensors_available; i++) {
		raw_read_energy_sensor(energy_sensors[i],
		                       &edpmm_global_counters.sensor_values[i].prev);
		printk(KERN_INFO "sensor[%i]=%llu\n",i,
		       edpmm_global_counters.sensor_values[i].prev);
		edpmm_global_counters.sensor_values[i].acum=0;
	}

	for (i=0; i<AMP_CORE_TYPES; i++)
		edpmm_global_counters.glb_instr_counter[i]=0;

	edpmm_global_counters.edp_counts_on=1; //Default enabled

	spin_lock_init(&edpmm_global_counters.lock);

	return 0;
}

int edp_release_resources(void)
{
	release_energy_sensors(energy_sensors,nr_sensors_available);
	return 0;
}

void edp_pause_global_counts(void)
{
	int i=0;
	uint64_t val=0;

	spin_lock(&edpmm_global_counters.lock);

	for (i=0; i<nr_sensors_available; i++) {
		raw_read_energy_sensor(energy_sensors[i],&val);
		edpmm_global_counters.sensor_values[i].acum=val-edpmm_global_counters.sensor_values[i].prev;
	}

	edpmm_global_counters.edp_counts_on=0;
	spin_unlock(&edpmm_global_counters.lock);
}

void edp_resume_global_counts(void)
{
	edpmm_global_counters.edp_counts_on=1;
}

void edp_reset_global_counters(void)
{
	int i=0;
	spin_lock(&edpmm_global_counters.lock);

	for (i=0; i<nr_sensors_available; i++) {
		raw_read_energy_sensor(energy_sensors[i],&edpmm_global_counters.sensor_values[i].prev);
		edpmm_global_counters.sensor_values[i].acum=0;
	}

	for (i=0; i<AMP_CORE_TYPES; i++) {
		edpmm_global_counters.glb_instr_counter[i]=0;
	}

	edpmm_global_counters.edp_counts_on=1; //Enabled on reset...

	spin_unlock(&edpmm_global_counters.lock);
}

void edp_update_global_instr_counter(uint64_t* thread_instr_counts)
{
	int i=0;
	char comm[TASK_COMM_LEN];

	if (!edpmm_global_counters.edp_counts_on)
		return;

	spin_lock(&edpmm_global_counters.lock);

	for (i=0; i<AMP_CORE_TYPES; i++)
		edpmm_global_counters.glb_instr_counter[i]+=thread_instr_counts[i];

	if (thread_instr_counts[AMP_FAST_CORE]+thread_instr_counts[AMP_SLOW_CORE]) {
		get_task_comm(comm,current);
		trace_printk("Thread finished. comm=%s, instr_big=%llu, instr_little=%llu\n",comm,thread_instr_counts[AMP_FAST_CORE],thread_instr_counts[AMP_SLOW_CORE]);
	}
	spin_unlock(&edpmm_global_counters.lock);
}


int edp_dump_global_counters(char* buf)
{
	uint64_t cur_val[MAX_VEXPRESS_ENERGY_SENSORS];
	uint64_t cur_instr_counter[AMP_CORE_TYPES];
	uint64_t val=0;
	int i=0;
	char* dst=buf;

	spin_lock(&edpmm_global_counters.lock);

	for (i=0; i<nr_sensors_available; i++) {
		cur_val[i]=0;
		if (edpmm_global_counters.edp_counts_on) {
			raw_read_energy_sensor(energy_sensors[i],&val);
			edpmm_global_counters.sensor_values[i].acum=val-edpmm_global_counters.sensor_values[i].prev;
		}
		cur_val[i]=edpmm_global_counters.sensor_values[i].acum;
	}
	for (i=0; i<AMP_CORE_TYPES; i++)
		cur_instr_counter[i]=edpmm_global_counters.glb_instr_counter[i];

	spin_unlock(&edpmm_global_counters.lock);

	for (i=0; i<nr_sensors_available; i++)
		dst+=sprintf(dst,"%s=%llu\n",energy_sensor_descr[i],cur_val[i]);

	for (i=0; i<AMP_CORE_TYPES; i++)
		dst+=sprintf(dst,"instr_coretype_%d=%llu\n",i,cur_instr_counter[i]);

	return dst-buf;
}

#elif defined(CONFIG_PMC_CORE_I7)
#include <pmc/intel_rapl.h>


typedef struct {
	intel_rapl_support_t rapl_support;
	uint64_t glb_instr_counter;
	volatile int edp_counts_on;
	spinlock_t lock;
} edpmm_global_data_t;

static edpmm_global_data_t edpmm_global_counters;

int edp_initialize_environment(void)
{
	int retval=0;

	if  ((retval=intel_rapl_initialize(&edpmm_global_counters.rapl_support, 1))){
		printk(KERN_INFO "Couldn't RAPL");
		return retval;
	}
	
	edpmm_global_counters.glb_instr_counter=0;

	edpmm_global_counters.edp_counts_on=1; //Default enabled

	spin_lock_init(&edpmm_global_counters.lock);

	return 0;
}

int edp_release_resources(void)
{
	intel_rapl_release(&edpmm_global_counters.rapl_support);
	return 0;
}

void edp_pause_global_counts(void)
{
	intel_rapl_update_energy_values(&edpmm_global_counters.rapl_support,1);
	edpmm_global_counters.edp_counts_on=0;
}

void edp_resume_global_counts(void)
{
	edpmm_global_counters.edp_counts_on=1;
}

void edp_reset_global_counters(void)
{
	intel_rapl_update_energy_values(&edpmm_global_counters.rapl_support,0);
	intel_rapl_reset_energy_values(&edpmm_global_counters.rapl_support);

	spin_lock(&edpmm_global_counters.lock);

	edpmm_global_counters.glb_instr_counter=0;

	edpmm_global_counters.edp_counts_on=1; //Enabled on reset...

	spin_unlock(&edpmm_global_counters.lock);
}

void edp_update_global_instr_counter(uint64_t* thread_instr_counts)
{
	char comm[TASK_COMM_LEN];
	uint64_t total_instr=0;

	if (!edpmm_global_counters.edp_counts_on)
		return;

	spin_lock(&edpmm_global_counters.lock);

	edpmm_global_counters.glb_instr_counter+=*thread_instr_counts;
	total_instr+=edpmm_global_counters.glb_instr_counter;
	

	if (total_instr) {
		get_task_comm(comm,current);
		trace_printk("Thread finished. comm=%s, total_instr=%llu\n",comm,total_instr);
	}
	spin_unlock(&edpmm_global_counters.lock);
}


int edp_dump_global_counters(char* buf)
{
	intel_rapl_sample_t rapl_sample;
	int i=0;
	char* dst=buf;
	int active_domains=0;
	uint64_t cur_instr_counter;
	intel_rapl_support_t* rapl_support=&edpmm_global_counters.rapl_support;


	/* Update */
	if (edpmm_global_counters.edp_counts_on)
		intel_rapl_update_energy_values(rapl_support,1);

	/* Read */
	intel_rapl_get_energy_sample(rapl_support,&rapl_sample);


	spin_lock(&edpmm_global_counters.lock);
	cur_instr_counter=edpmm_global_counters.glb_instr_counter;
	spin_unlock(&edpmm_global_counters.lock);

	for (i=0; i<RAPL_NR_DOMAINS; i++){
		if (rapl_support->available_power_domains_mask & (1<<i)){
			dst+=sprintf(dst,"%s=%llu\n",rapl_support->available_vcounters[active_domains],rapl_sample.energy_value[i]);
			active_domains++;
		}
	}

	dst+=sprintf(dst,"instr_coretype_0=%llu\n",cur_instr_counter);
	dst+=sprintf(dst,"instr_coretype_1=0\n");

	return dst-buf;
}

intel_rapl_support_t* get_global_rapl_handler(void){
	return &edpmm_global_counters.rapl_support;
}


#endif



