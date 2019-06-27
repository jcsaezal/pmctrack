/*
 *  smart_power_2_mm.c
 *
 * 	PMCTrack monitoring module enabling to obtain power measurements
 *  with Odroid Smart Power 2
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *                2019 Germán Franco Dorca <germanfr@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/rwlock.h>
#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/monitoring_mod.h>
#include <pmc/data_str/cbuffer.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
// for signal_pending
#include <linux/sched/signal.h>
#endif

#define SPOWER2_MODULE_STR "Odroid Smart Power 2"
#define SPOWER2_INPUT_FILE_PATH "/dev/ttyUSB0"
#define CBUFFER_CAPACITY 16

/* Structure to represent a sample gathered by Odroid SmartPower */
struct spower2_sample {
	int m_volts;
	int m_ampere;
	int m_watt;
	int m_watthour;
	int m_ujoules;
	unsigned long timestamp;
};

/* Per-thread private data for this monitoring module */
typedef struct {
	struct spower2_sample last_sample;
	unsigned long time_last_sample;
	int security_id;
} spower2_thread_data_t;


struct spower2_ctl {
	struct task_struct* reader_thread; /* Thread that keeps putting data into the buffer */
	struct file* input_file;           /* Pointer to the data input file */
	cbuffer_t* cbuffer;                /* Sample buffer */
	unsigned long time_last_sample;    /* Required for energy estimation */
	uint64_t cummulative_energy;       /* Counter to keep track of cumulative energy consumption */
	rwlock_t lock;                     /* RW lock for the various fields */
} spower2_gbl;

enum {SPOWER2_POWER=0,SPOWER2_CURRENT,SPOWER2_ENERGY,SPOWER2_NR_MEASUREMENTS};


static int get_summary_samples(unsigned long timestamp, struct spower2_sample* sample);
static int spower2_get_sample(unsigned long from, struct spower2_sample* sample);
static int spower2_parse_float(const char* str, int* val);
static noinline int spower2_parse_sample(const char* str, struct spower2_sample* sample);
static int spower2_read_from_file(struct spower2_sample* sample);
static int spower2_thread_fn(void *data);


/*
 * [PMCTrack API]
 * Return the capabilities/properties of this monitoring module.
 */
static void spower2_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	usage->hwpmc_mask=0;
	usage->nr_virtual_counters=SPOWER2_NR_MEASUREMENTS;
	usage->nr_experiments=0;
	usage->vcounter_desc[SPOWER2_POWER]="power_mw";
	usage->vcounter_desc[SPOWER2_CURRENT]="current_ma";
	usage->vcounter_desc[SPOWER2_ENERGY]="energy_uj";
}

/*
 * [PMCTrack API]
 * MM initialization
 */
static int spower2_enable_module(void)
{
	// Open input file and start reading measurements
	struct file* input_file = filp_open(SPOWER2_INPUT_FILE_PATH, O_RDONLY, 0);
	if(IS_ERR(input_file)) {
		return PTR_ERR(input_file);
	}
	spower2_gbl.input_file = input_file;


	spower2_gbl.cbuffer = create_cbuffer_t(CBUFFER_CAPACITY * sizeof(struct spower2_sample));
	if (!spower2_gbl.cbuffer) { // Free previously opened resources
		filp_close(spower2_gbl.input_file, NULL);
		spower2_gbl.input_file = NULL;
		return -ENOMEM;
	}

	rwlock_init(&spower2_gbl.lock);

	spower2_gbl.reader_thread = kthread_run(spower2_thread_fn, NULL, "spower-reader");
	if (!spower2_gbl.reader_thread) { // Free previously opened resources
		filp_close(spower2_gbl.input_file, NULL);
		spower2_gbl.input_file = NULL;
		destroy_cbuffer_t(spower2_gbl.cbuffer);
		spower2_gbl.cbuffer = NULL;
		return -1;
	}

	return 0;
}

/*
 * [PMCTrack API]
 * MM cleanup function
 */
static void spower2_disable_module(void)
{
	// Stop reading measuremennts and close input file

	if (spower2_gbl.reader_thread) {
		kthread_stop(spower2_gbl.reader_thread);
		spower2_gbl.reader_thread = NULL;
	}

	if (spower2_gbl.input_file) {
		filp_close(spower2_gbl.input_file, NULL);
		spower2_gbl.input_file = NULL;
	}

	if (spower2_gbl.cbuffer) {
		destroy_cbuffer_t(spower2_gbl.cbuffer);
		spower2_gbl.cbuffer = NULL;
	}

	printk(KERN_ALERT "%s monitoring module unloaded!!\n", SPOWER2_MODULE_STR);
}

/*
 * [PMCTrack API]
 * Display configuration parameters
 */
static int spower2_on_read_config(char* str, unsigned int len)
{
	char* dst=str;

	dst += sprintf(dst, "spower2_cummulative_energy = %llu\n", spower2_gbl.cummulative_energy);

	return dst - str;
}

/*
 * [PMCTrack API]
 * Change configuration parameters
 */
static int spower2_on_write_config(const char *str, unsigned int len)
{
	if (strncmp(str, "reset_energy_count", 18) == 0) {
		spower2_gbl.cummulative_energy = 0;
	}
	return 0;
}

/*
 * [PMCTrack API]
 * on fork() callback
 */
static int spower2_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{
	spower2_thread_data_t*  data= NULL;

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;


	data= kmalloc(sizeof (spower2_thread_data_t), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	memset(&data->last_sample,0,sizeof(struct spower2_sample));
	data->time_last_sample=jiffies;
	data->security_id=current_monitoring_module_security_id();
	prof->monitoring_mod_priv_data = data;
	return 0;
}

/* Custom version to parse float that uses 1000 as the default scale factor */
static int spower2_parse_float(const char* str, int* val)
{
	int len = 0;
	int unit = 0;
	int dec = 0;

	while (!isdigit(*str) && (*str) != '-') {
		str++;
		len++;
	}

	if (sscanf(str, "%d.%d", &unit, &dec) != 2)
		(*val) = -1;
	else
		(*val) = unit * 1000 + dec;

#ifdef DEBUG
	trace_printk("unit=%d,dec=%d\n", unit, dec);
#endif

	if (*str == '-') {
		/* Move forward 5 positions */
		/* -.--- */
		return len + 5;
	} else {
		while (isdigit(*str) || (*str) == '.') {
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
	4.031,0.674,2.725,0.000

*/
static noinline int spower2_parse_sample(const char* str, struct spower2_sample* sample)
{
	if (strlen(str) < 23) {
		sample->m_volts = sample->m_ampere = sample->m_watt = sample->m_watthour = -1;
		return 1;
	}

	str += spower2_parse_float(str, &sample->m_volts);
	str += spower2_parse_float(str, &sample->m_ampere);
	str += spower2_parse_float(str, &sample->m_watt);
	str += spower2_parse_float(str, &sample->m_watthour);
	return 0;
}

// This is just a wrapper
static inline ssize_t spower2_readfile(struct file *file, void *buf, unsigned long count)
{
	// kernel_read is a wrapper for vfs_read with get_fs/set_fs already
	// NOTICE: kernel_read prototype changed in linux >= 4.14 (this is the old one)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	return kernel_read(file, 0, buf, count);
#else
	static loff_t offset = 0;
	return kernel_read(file, buf, count, &offset);
#endif
}

/* Read a sample from the input file */
static int spower2_read_from_file(struct spower2_sample* sample)
{
	char kbuf[24] = {0}; /* 24 is the length of the expected string */
	int ret;

	if ((ret = spower2_readfile(spower2_gbl.input_file, kbuf, sizeof(kbuf))) > 0) {
		return spower2_parse_sample(kbuf, sample);
	}

	return ret ? ret : -1;
}

static int spower2_thread_fn(void *data)
{
	struct spower2_sample sample = {0};
	unsigned long flags;
	unsigned long now;

	allow_signal(SIGKILL);
	while (!kthread_should_stop() && !signal_pending(spower2_gbl.reader_thread)) {
		now = jiffies; // Save current time to use he same accross multiple calculations

		if (spower2_read_from_file(&sample) == 0) {
			sample.timestamp = now;
			sample.m_ujoules = sample.m_watt * (now - spower2_gbl.time_last_sample) * 1000 / HZ;
			spower2_gbl.cummulative_energy += sample.m_ujoules;
			spower2_gbl.time_last_sample = now;

			write_lock_irqsave(&spower2_gbl.lock, flags);
			insert_items_cbuffer_t(spower2_gbl.cbuffer, &sample, sizeof(struct spower2_sample));
			write_unlock_irqrestore(&spower2_gbl.lock, flags);
		}
	}

#ifdef DEBUG
	pr_info(SPOWER2_MODULE_STR " thread stopping\n");
#endif
	do_exit(0);
	return 0;
}

static int get_summary_samples(unsigned long timestamp, struct spower2_sample* sample)
{
	int nr_samples = 0;
	struct spower2_sample* cur;
	iterator_cbuffer_t it = get_iterator_cbuffer_t(spower2_gbl.cbuffer, sizeof(struct spower2_sample));

	memset(sample, 0, sizeof(struct spower2_sample));

	while ((cur = iterator_next_cbuffer_t(&it))) {
		if (cur->timestamp >= timestamp) {
			sample->m_volts += cur->m_volts;
			sample->m_ampere += cur->m_ampere;
			sample->m_watt += cur->m_watt;
			sample->m_watthour += cur->m_watthour;
			sample->m_ujoules += cur->m_ujoules;
			nr_samples++;
		}
	}

	if (nr_samples > 0) {
		sample->m_volts /= nr_samples;
		sample->m_ampere /= nr_samples;
		sample->m_watt /= nr_samples;
		/* Joules (and watt-hour) are cummulative, so no division here */
	}

	return nr_samples;
}

/*
 * Get a sample that summarizes measurements collected from a given point
 * The function returns the number of samples in the buffer used to obtain
 * that sample (average).
 */
static int spower2_get_sample(unsigned long from, struct spower2_sample* sample)
{
	int nr_samples;
	unsigned long flags;
	read_lock_irqsave(&spower2_gbl.lock, flags);
	nr_samples = get_summary_samples(from, sample);
	read_unlock_irqrestore(&spower2_gbl.lock, flags);
	return nr_samples;
}

/*
 * [PMCTrack API]
 * Update cumulative energy counters in the thread structure and
 * set the associated virtual counts in the PMC sample structure
 */
static int spower2_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data)
{
	spower2_thread_data_t* tdata=prof->monitoring_mod_priv_data;
	int i=0;
	int cnt_virt=0;

	if (tdata==NULL || prof->virt_counter_mask==0)
		return 0;

	/* dump data if we got something */
	if (spower2_get_sample(tdata->time_last_sample,&tdata->last_sample)==0)
		return 0;

	tdata->time_last_sample=jiffies;

	/* Embed virtual counter information so that the user can see what's going on */
	for (i=0; i<SPOWER2_NR_MEASUREMENTS; i++) {
		if ((prof->virt_counter_mask & (1<<i)) ) {
			switch (i) {
			case SPOWER2_POWER:
				sample->virtual_counts[cnt_virt++]=tdata->last_sample.m_watt;
				break;
			case SPOWER2_CURRENT:
				sample->virtual_counts[cnt_virt++]=tdata->last_sample.m_ampere;
				break;
			case SPOWER2_ENERGY:
				sample->virtual_counts[cnt_virt++]=tdata->last_sample.m_ujoules;
				break;
			default:
				continue;
			}

			sample->virt_mask|=(1<<i);
			sample->nr_virt_counts++;
		}
	}

	return 0;
}

/*
 * [PMCTrack API]
 * Free up private data
 */
static void spower2_on_free_task(pmon_prof_t* prof)
{
	if (prof->monitoring_mod_priv_data)
		kfree(prof->monitoring_mod_priv_data);
}

/* Implementation of the monitoring_module_t interface */
monitoring_module_t spower2_mm= {
	.info=SPOWER2_MODULE_STR,
	.id=-1,
	.enable_module=spower2_enable_module,
	.disable_module=spower2_disable_module,
	.on_read_config=spower2_on_read_config,
	.on_write_config=spower2_on_write_config,
	.on_fork=spower2_on_fork,
	.on_new_sample=spower2_on_new_sample,
	.on_free_task=spower2_on_free_task,
	.module_counter_usage=spower2_module_counter_usage,
};
