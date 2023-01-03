/*
 *  intel_ehfi.c
 *
 *  Configuration code for Intel's Enhanced Hardware Feedback interface
 *
 *  Copyright (c) 2022 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */
/*
 * Much of this code found in this file is based
 * on that of the patch submitted by intel, which is available here:
 * https://lwn.net/ml/linux-kernel/20211220151438.1196-1-ricardo.neri-calderon@linux.intel.com/
 */

#include <pmc/intel_ehfi.h>
#include <linux/bits.h>
#include <linux/slab.h>
#include <pmc/pmu_config.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <pmc/mc_experiments.h>

#define X86_FEATURE_HFI			(14*32+19) /* Hardware Feedback Interface */
#define EHFI_UPDATE_INTERVAL	HZ

/* Hardware Feedback Interface */
#define PMCT_MSR_IA32_HW_FEEDBACK_PTR        0x17d0
#define PMCT_MSR_IA32_HW_FEEDBACK_CONFIG     0x17d1
#define PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG     0x17d4
#define PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CHAR     0x17d2
#define PMCT_MSR_IA32_PACKAGE_THERM_STATUS		0x000001b1
#define PMCT_MSR_IA32_PACKAGE_THERM_INTERRUPT	0x000001b2
#define PMCT_MSR_IA32_HRESET_ENABLE 0x17da
#define THERM_STATUS_CLEAR_PKG_MASK (BIT(1) | BIT(3) | BIT(5) | BIT(7) | \
				     BIT(9) | BIT(11) | BIT(26))

/* Hardware Feedback Interface MSR configuration bits */
#define HW_FEEDBACK_PTR_VALID_BIT		BIT(0)
#define HW_FEEDBACK_CONFIG_HFI_ENABLE_BIT	BIT(0)
#define HW_FEEDBACK_THREAD_CONFIG_ENABLE_BIT	BIT(0)
#define HRESET_ENABLE_BIT	BIT(0)
#define HW_FEEDBACK_THREAD_CHAR_VALID_BIT	BIT(63)
#define HW_FEEDBACK_CONFIG_THREAD_SPECIFIC_ENABLE_BIT BIT(1)
#define PACKAGE_THERM_INT_EHFI_ENABLE		(1 << 25)
#define PACKAGE_THERM_STATUS_EHFI_UPDATED	(1 << 26)


/* CPUID detection and enumeration definitions for HFI */

#define CPUID_HFI_LEAF 6
#define CPUID_HFI_LEAF_EHFI_SUPPORTED BIT(23)


union ehfi_capabilities {
	struct {
		u8	performance:1;
		u8	energy_efficiency:1;
		u8	__reserved:6;
	} split;
	u8 bits;
};

union cpuid6_edx {
	struct {
		union ehfi_capabilities	capabilities;
		u32			table_pages:4;
		u32			__reserved:4;
		s32			index:16;
	} split;
	u32 full;
};

union cpuid6_eax {
	struct {
		u32			__ignored1:18;
		u32			hfi_supported:1;
		u32			__ignored2:3;
		u32			ehfi_supported:1;
	} split;
	u32 full;
};


union cpuid6_ecx {
	struct {
		u32			hardware_coordination_feedback:1;
		u32			__reserved:2;
		u32			perf_energy_bias:1;
		u32			__reserved2:4;
		u32			ehfi_class_count:8;
	} split;
	u32 full;
};
/**
 * struct ehfi_hdr - Header of the HFI table
 * @perf_updated:	Hardware updated performance capabilities
 * @ee_updated:		Hardware updated energy efficiency capabilities
 *
 * Properties of the data in an HFI table.
 */
struct ehfi_hdr {
	u8	perf_updated;
	u8	ee_updated;
} __packed;


struct scaled_capabilities {
	unsigned int	perf;
	unsigned int	eef;
};


/**
 * struct ehfi_instance - Representation of an EHFI instance (i.e., a table)
 * @hw_table:		Pointer to the EHFI table of this instance
 * @cpus:		CPUs represented in this EHFI table instance
 * @table_lock:		Lock to protect acceses to the table of this instance
 * A set of parameters to parse and navigate a specific EHFI table.
 */
struct ehfi_instance {
	void			*hw_table;
	phys_addr_t 	hw_table_pa;
	void			*hdr;
	void			*data;
	raw_spinlock_t	table_lock;
	bool			initialized;
	struct delayed_work	update_work;
	raw_spinlock_t		event_lock;
	u64 prev_timestamp;
	struct timer_list timer;
};

/**
 * struct ehfi_features - Supported EHFI features
 * @nr_table_pages:	Size of the EHFI table in 4KB pages
 * @cpu_stride:		Stride size to locate capability data of a logical
 *			processor within the table (i.e., row stride)
 * @hdr_size:		Size of table header
 *
 * Parameters and supported features that are common to all HFI instances
 */
struct ehfi_features {
	unsigned int	nr_table_pages;
	unsigned int	cpu_stride;
	unsigned int	hdr_size;
	unsigned int 	nr_classes;
	union ehfi_capabilities	capabilities;
	unsigned int nr_lp_entries;
	unsigned long active_lp_indexes;
};


/**
 * struct ehfi_cpu_info - Per-CPU attributes to consume EHFI data
 * @index:		Row of this CPU in its EHFI table
 * @ehfi_instance:	Attributes of the EHFI table to which this CPU belongs
 *
 * Parameters to link a logical processor to an EHFI table and a row within it.
 */
struct ehfi_cpu_info {
	s16			index;
	u16 		die_id;
	struct ehfi_instance	*ehfi_instance;
};


static DEFINE_PER_CPU(struct ehfi_cpu_info, ehfi_cpu_info) = { .index = -1 };

unsigned char enable_hfi=0; /* Change that at boot only */

static int max_ehfi_instances;
static struct ehfi_instance *ehfi_instances;

static struct ehfi_features ehfi_features;

/* Private proc entry */
extern struct proc_dir_entry *pmc_dir;
static struct proc_dir_entry *ehfi_proc=NULL;

static ssize_t ehfi_proc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t ehfi_proc_read(struct file *filp, char __user *buf, size_t len, loff_t *off);

static pmctrack_proc_ops_t fops = {
	.PMCT_PROC_READ = ehfi_proc_read,
	.PMCT_PROC_WRITE = ehfi_proc_write,
	.PMCT_PROC_LSEEK = default_llseek
};

#define PBUFSIZ 100

static ssize_t ehfi_proc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	char kbuf[PBUFSIZ];
	struct ehfi_instance *ehfi_instance;
	int cpu = smp_processor_id();
	struct ehfi_cpu_info *info;
	unsigned long flags=0;
	unsigned char* contents;
	int nr_classes=ehfi_features.nr_classes;
	int i;


	if (len>=PBUFSIZ)
		return -ENOSPC;

	if (copy_from_user(kbuf,buf,len))
		return -EFAULT;

	kbuf[len]=0;

	if (strcmp(kbuf,"clear_flags\n")==0) {

		info = &per_cpu(ehfi_cpu_info, cpu);
		if (!info)
			return -ENODEV;

		ehfi_instance = info->ehfi_instance;
		if (!ehfi_instance)
			return -ENODEV;

		raw_spin_lock_irqsave(&ehfi_instance->table_lock,flags);

		contents=((unsigned char*)ehfi_instance->hw_table)+8;

		/* Clear change bits */
		for (i=0; i<nr_classes; i++) {
			contents[0]=contents[1]=0;
			contents+=2;
		}

		raw_spin_unlock_irqrestore(&ehfi_instance->table_lock,flags);


		return len;
	}

	return -EINVAL; /* For now it is not supported */
}


static ssize_t ehfi_proc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	int nr_bytes = 0;
	char *dest,*kbuf;
	int err,i,j;
	struct ehfi_instance *ehfi_instance;
	int cpu = smp_processor_id();
	struct ehfi_cpu_info *info;
	unsigned long flags=0;
	unsigned char* contents;
	int nr_classes=ehfi_features.nr_classes;
	u64* timestamp;

	if (*off>0)
		return 0;

	info = &per_cpu(ehfi_cpu_info, cpu);
	if (!info)
		return -ENODEV;

	ehfi_instance = info->ehfi_instance;
	if (!ehfi_instance)
		return -ENODEV;

	kbuf=vmalloc(PAGE_SIZE);

	if (!kbuf)
		return -ENOMEM;

	dest=kbuf;

	raw_spin_lock_irqsave(&ehfi_instance->table_lock,flags);

	contents=((unsigned char*)ehfi_instance->hw_table)+8;
	timestamp=(u64*)ehfi_instance->hw_table;

	dest+=sprintf(dest,"Timestamp: 0x%llx\n",*timestamp);

	/* Print change bits */
	for (i=0; i<nr_classes; i++) {
		dest+=sprintf(dest,"Modified_Class%i: perf=%x and eef=%x\n",i,contents[0],contents[1]);
		contents+=2;
	}

	for (j=0; j<16; j++) { /* Assume 16 cores */
		if (ehfi_features.active_lp_indexes & (1<<j)) {
			for (i=0; i<nr_classes; i++) {
				dest+=sprintf(dest,"Data_LP%i_Class%i: perf=%x and eef=%x\n",j, i,contents[0],contents[1]);
				contents+=2;
			}
		} else {
			contents+=nr_classes*2;
		}
	}

	raw_spin_unlock_irqrestore(&ehfi_instance->table_lock,flags);

	nr_bytes=dest-kbuf;

	if (copy_to_user(buf, kbuf, nr_bytes) > 0) {
		err=-EFAULT;
		goto err_config_read;
	}

	(*off) += nr_bytes;

	vfree(kbuf);
	return nr_bytes;
err_config_read:
	vfree(kbuf);
	return err;
}


static inline void get_row_info_cpu(struct ehfi_instance * ehfi_instance,
                                    int cpu_index, int class,
                                    unsigned int *perf, unsigned int *eef)
{
	unsigned const int nr_capabilities=2;
	unsigned int class_data_offset=ehfi_features.nr_classes*nr_capabilities; /* Assume is multiple of 8 */
	unsigned char* contents=((unsigned char*)ehfi_instance->hw_table)+8+class_data_offset; /* Skip timestamp and modified section header */
	unsigned char raw_perf, raw_eef;

	contents+=cpu_index*class_data_offset+class*nr_capabilities; /* Point to the right row (LP entry + class position) */

	/* Read data */
	raw_perf=contents[0];
	raw_eef=contents[1];

	/* Return values in normalized format  */
	(*perf)=((unsigned int)raw_perf)<<2;
	(*eef)=((unsigned int)raw_eef)<<2;
}


static inline int get_current_class(void)
{
	int class=-1;
	u64 msr_val;

	rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CHAR, msr_val);

	if (msr_val & HW_FEEDBACK_THREAD_CHAR_VALID_BIT)
		class=(msr_val & 0xff);

	return class;
}

int get_current_ehfi_class(void)
{
	int cpu = smp_processor_id();
	struct ehfi_cpu_info *info;
	struct ehfi_instance *ehfi_instance;

	info = &per_cpu(ehfi_cpu_info, cpu);
	if (!info || info->index == -1)
		return -ENODEV;

	ehfi_instance = info->ehfi_instance;
	if (!ehfi_instance)
		return -ENODEV;

	return get_current_class();
}

int get_current_ehfi_info(unsigned int last_class, struct ehfi_thread_info* tinfo)
{
	int cpu = smp_processor_id();
	struct ehfi_cpu_info *info;
	struct ehfi_cpu_info *remote_info;
	struct ehfi_instance *ehfi_instance;
	int current_class;
	int current_coretype=get_coretype_cpu(cpu);
	int other_coretype=current_coretype==1?0:1;
	int other_cpu=get_any_cpu_coretype(other_coretype);
	struct scaled_capabilities table_data[2];

	info = &per_cpu(ehfi_cpu_info, cpu);
	if (!info || info->index==-1)
		return -ENODEV;

	ehfi_instance = info->ehfi_instance;
	if (!ehfi_instance)
		return -ENODEV;

	tinfo->ehfi_class=get_current_class();

	if (tinfo->ehfi_class==-1)
		current_class=last_class;
	else
		current_class=tinfo->ehfi_class;

	/* Point to remote data */
	remote_info = &per_cpu(ehfi_cpu_info, other_cpu);

	if (!remote_info || remote_info->index==-1)
		return -ENODEV;

	/* Retrieve class info from both core types */
	get_row_info_cpu(ehfi_instance,info->index,current_class,
	                 &table_data[current_coretype].perf,
	                 &table_data[current_coretype].eef);
	get_row_info_cpu(ehfi_instance,remote_info->index,current_class,
	                 &table_data[other_coretype].perf,
	                 &table_data[other_coretype].eef);

	/* Update data */
	tinfo->perf=table_data[current_coretype].perf;
	tinfo->eef=table_data[current_coretype].eef;

	if (current_coretype==1) { /* Big core*/
		/* Control divide by zero */
		if (table_data[other_coretype].perf==0)
			tinfo->perf_ratio=1000; /* Data unavailable*/
		else
			tinfo->perf_ratio=(table_data[current_coretype].perf*1000)/table_data[other_coretype].perf;

		if (table_data[other_coretype].eef==0)
			tinfo->eef_ratio=1000; /* Data unavailable*/
		else
			tinfo->eef_ratio=(table_data[current_coretype].eef*1000)/table_data[other_coretype].eef;
	} else {
		/* Control divide by zero */
		if (table_data[current_coretype].perf==0)
			tinfo->perf_ratio=1000; /* Data unavailable*/
		else
			tinfo->perf_ratio=(table_data[other_coretype].perf*1000)/table_data[current_coretype].perf;

		if (table_data[current_coretype].eef==0)
			tinfo->eef_ratio=1000;
		else
			tinfo->eef_ratio=(table_data[other_coretype].eef*1000)/table_data[current_coretype].eef;

	}
	return 0;
}

static int ehfi_parse_features(void)
{
	unsigned int nr_capabilities;
	union cpuid6_edx edx;
	union cpuid6_eax eax;
	union cpuid6_ecx ecx;
	cpuid_regs_t rv;

	if (!boot_cpu_has(X86_FEATURE_HFI)) {
		pr_info("HFI not supported\n");
		return -ENODEV;
	}

	/*
	 * If we are here we know that CPUID_EHFI_LEAF exists. Parse the
	 * supported capabilities and the size of the HFI table.
	 */
	//edx.full= pmct_cpuid_edx(CPUID_HFI_LEAF);

	rv.eax=CPUID_HFI_LEAF;
	rv.ecx=0;
	run_cpuid(rv);
	eax.full=rv.eax;
	ecx.full=rv.ecx;
	edx.full=rv.edx;

	if (!eax.split.ehfi_supported) {
		pr_info("EHFI not supported\n");
		return -ENODEV;
	}

	if (!edx.split.capabilities.split.performance) {
		pr_debug("Performance reporting not supported! Not using EHFI\n");
		return -ENODEV;
	}

	/*
	 * The number of supported capabilities determines the number of
	 * columns in the EHFI table. Exclude reserved bits.
	 */
	edx.split.capabilities.split.__reserved = 0;
	nr_capabilities = hweight8(edx.split.capabilities.bits);

	ehfi_features.capabilities=edx.split.capabilities;

	/* The number of 4KB pages required by the table */
	ehfi_features.nr_table_pages = edx.split.table_pages + 1;

	/*
	 * The header contains change indications for each supported feature.
	 * The size of the table header is rounded up to be a multiple of 8
	 * bytes.
	 */
	ehfi_features.hdr_size = DIV_ROUND_UP(nr_capabilities, 8) * 8;

	/*
	 * Data of each logical processor is also rounded up to be a multiple
	 * of 8 bytes.
	 */
	ehfi_features.cpu_stride = DIV_ROUND_UP(nr_capabilities, 8) * 8;

	ehfi_features.nr_classes = ecx.split.ehfi_class_count;
	return 0;
}

static inline u64 read_updated_table_bit(void)
{
	u64 status_val;
	/* Wait until BIT 26 is set to one */
	rdmsrl(PMCT_MSR_IA32_PACKAGE_THERM_STATUS, status_val);
	return status_val & PACKAGE_THERM_STATUS_EHFI_UPDATED;
}


static inline void clear_update_table_bit(void)
{
	u64 status_val;
	/* Disable Bit, due to changes... */
	//rdmsrl(PMCT_MSR_IA32_PACKAGE_THERM_STATUS, status_val);
	status_val&=THERM_STATUS_CLEAR_PKG_MASK;
	wrmsrl(PMCT_MSR_IA32_PACKAGE_THERM_STATUS, status_val & (~PACKAGE_THERM_STATUS_EHFI_UPDATED));
}


#ifndef CONFIG_INTEL_THREAD_DIRECTOR
/* Function invoked when timer expires (fires) */
static void ehfi_test_timer(struct timer_list *timer)
{
	bool modified_table=false;
	struct ehfi_instance *ehfi_instance;
	u64 msr_val;
	u64* timestamp;
	ehfi_instance = container_of(timer, struct ehfi_instance,
	                             timer);

	timestamp=(u64*)ehfi_instance->hw_table;

	modified_table=read_updated_table_bit();

	rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_CONFIG, msr_val);
	trace_printk("Mod=%d Timestamp=0x%llx MSR_IA32_HW_FEEDBACK_CONFIG=%llu\n",modified_table,*timestamp,msr_val);
	rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CHAR, msr_val);
	trace_printk("PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CHAR=%llu\n",msr_val);

//#define FORCE_HRESET
#ifdef FORCE_HRESET
	if (!modified_table) {
		hreset(0x1);
	} else {
		clear_update_table_bit();
	}
#endif
	/* Re-activate the timer one second from now */
	mod_timer(timer, jiffies + HZ);
}
#endif

static void ehfi_update_work_fn(struct work_struct *work)
{
	struct ehfi_instance *ehfi_instance;

	ehfi_instance = container_of(to_delayed_work(work), struct ehfi_instance,
	                             update_work);
	if (!ehfi_instance)
		return;

	/* TODO: Consume update here. */
	/* MAYBE INTERRUPTS COULD BE REENABLED HERE!!! */
}


static int setup_hw_table(struct ehfi_instance *ehfi_instance)
{
	u64 msr_val;
	u32 l, h;
	phys_addr_t old_hw_table=0;
	/* Read ptr msr and if it is not 0 do not allocate memory, but reuse */

	ehfi_instance->prev_timestamp=0;

	/* Check for a previous configuration of the EHFI table */
	rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_PTR, msr_val);
	old_hw_table=msr_val & ~(HW_FEEDBACK_CONFIG_HFI_ENABLE_BIT | HW_FEEDBACK_CONFIG_THREAD_SPECIFIC_ENABLE_BIT);

#ifdef CONFIG_INTEL_THREAD_DIRECTOR
	if (!old_hw_table) {
		pr_err("Error: Thread Director Table should have been configured at this point by the kernel\n");
		return -ENOTSUPP;
	}
#endif
	if (old_hw_table) {
		ehfi_instance->hw_table_pa=old_hw_table;
		ehfi_instance->hw_table=phys_to_virt(ehfi_instance->hw_table_pa);
	} else {
		/* Enable interrupts */
		rdmsr(PMCT_MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
		wrmsr(PMCT_MSR_IA32_PACKAGE_THERM_INTERRUPT,
		      l | PACKAGE_THERM_INT_EHFI_ENABLE, h);

		/*
		 * Hardware is programmed with the physical address of the first page
		 * frame of the table. Hence, the allocated memory must be page-aligned.
		 */
		ehfi_instance->hw_table = alloc_pages_exact(ehfi_features.nr_table_pages,
		                          GFP_KERNEL | __GFP_ZERO); /* Already set to 0 */
		if (!ehfi_instance->hw_table)
			return -ENOMEM;

		ehfi_instance->hw_table_pa = virt_to_phys(ehfi_instance->hw_table);

		/*
		 * Enable the hardware feedback interface and never disable it. See
		 * comment on programming the address of the table.
		 */
		rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_CONFIG, msr_val);
		if (!enable_hfi)
			msr_val |= (HW_FEEDBACK_CONFIG_HFI_ENABLE_BIT | HW_FEEDBACK_CONFIG_THREAD_SPECIFIC_ENABLE_BIT);
		else
			msr_val |= HW_FEEDBACK_CONFIG_HFI_ENABLE_BIT;
		wrmsrl(PMCT_MSR_IA32_HW_FEEDBACK_CONFIG, msr_val);

		/*
		 * Program the address of the feedback table of this die/package. On
		 * some processors, hardware remembers the old address of the HFI table
		 * even after having been reprogrammed and re-enabled. Thus, do not free
		 * pages allocated for the table or reprogram the hardware with a new
		 * base address. Namely, program the hardware only once.
		 */
		msr_val = ehfi_instance->hw_table_pa | HW_FEEDBACK_PTR_VALID_BIT;
		wrmsrl(PMCT_MSR_IA32_HW_FEEDBACK_PTR, msr_val);
	}

	/* The HFI header is below the time-stamp. */
	ehfi_instance->hdr = ehfi_instance->hw_table; //+sizeof(*hfi_instance->timestamp);

	/* The HFI data starts below the header. */
	ehfi_instance->data = ehfi_instance->hdr + ehfi_features.hdr_size;

	INIT_DELAYED_WORK(&ehfi_instance->update_work, ehfi_update_work_fn);
	raw_spin_lock_init(&ehfi_instance->table_lock);
	raw_spin_lock_init(&ehfi_instance->event_lock);

#ifdef ENABLE_TIMER
	/* Create timer */
	timer_setup(&ehfi_instance->timer, ehfi_test_timer, 0);
	ehfi_instance->timer.expires=jiffies + HZ;  /* Activate it one second from now */
	/* Activate the timer for the first time */
	add_timer_on(&ehfi_instance->timer,2);
#endif

	ehfi_instance->initialized = true;

	return 0;
}

static void disable_hw_table(struct ehfi_instance *ehfi_instance)
{
	if (ehfi_proc) {
		ehfi_proc=NULL;
		remove_proc_entry("ehfi", pmc_dir);
	}

	flush_delayed_work(&ehfi_instance->update_work);
#ifdef ENABLE_TIMER
	/* Wait until completion of the timer function (if it's currently running) and delete timer */
	del_timer_sync(&ehfi_instance->timer);
#endif
	ehfi_instance->initialized = false;
}


void enable_ehfi_thread(void)
{
#ifndef CONFIG_INTEL_THREAD_DIRECTOR
	u64 msr_val;

	rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
	msr_val|=HW_FEEDBACK_THREAD_CONFIG_ENABLE_BIT;
	wrmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
#endif
}

void disable_ehfi_thread(void)
{
#ifndef CONFIG_INTEL_THREAD_DIRECTOR
	u64 msr_val;

	rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
	msr_val&=~HW_FEEDBACK_THREAD_CONFIG_ENABLE_BIT;
	wrmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
#endif
}

static void init_cpu_instance(void* dummy)
{
#ifndef CONFIG_INTEL_THREAD_DIRECTOR
	u64 msr_val;
#endif
	int cpu=smp_processor_id();
	union cpuid6_edx edx;
	struct ehfi_cpu_info *info = &per_cpu(ehfi_cpu_info, cpu);
	u16 die_id = topology_logical_die_id(cpu);

	info->ehfi_instance = &ehfi_instances[die_id];
	edx.full = pmct_cpuid_edx(CPUID_HFI_LEAF);
	info->index = edx.split.index;
	info->die_id=die_id;

#ifndef CONFIG_INTEL_THREAD_DIRECTOR
	if (!enable_hfi) {
		rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
		msr_val|=HW_FEEDBACK_THREAD_CONFIG_ENABLE_BIT;
		wrmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
	}

	/* This is a per-cpu register */
	/* Enable HRESET */
	rdmsrl(PMCT_MSR_IA32_HRESET_ENABLE, msr_val);
	wrmsrl(PMCT_MSR_IA32_HRESET_ENABLE, msr_val |  HRESET_ENABLE_BIT);
#endif
}

static void unlink_cpu_instance(void* dummy)
{
#ifndef CONFIG_INTEL_THREAD_DIRECTOR
	u64 msr_val;
#endif
	int cpu=smp_processor_id();
	struct ehfi_cpu_info *info = &per_cpu(ehfi_cpu_info, cpu);

	info->ehfi_instance = NULL;
	info->index = -1;

#ifndef CONFIG_INTEL_THREAD_DIRECTOR
	if (!enable_hfi) {
		rdmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
		msr_val&=~HW_FEEDBACK_THREAD_CONFIG_ENABLE_BIT;
		wrmsrl(PMCT_MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
	}

	rdmsrl(PMCT_MSR_IA32_HRESET_ENABLE, msr_val);
	wrmsrl(PMCT_MSR_IA32_HRESET_ENABLE, msr_val &~HRESET_ENABLE_BIT);
#endif
}

int intel_ehfi_initialize(void)
{
	int ret=0;
	unsigned long cpu=smp_processor_id();
	u16 die_id = topology_logical_die_id(cpu);
	unsigned long i=0;
	int nr_cpus=num_online_cpus();
	struct ehfi_cpu_info *info;

	if (ehfi_parse_features())
		return -ENODEV;


	/* There is one HFI instance per die/package. */
	max_ehfi_instances = topology_max_packages() *
	                     topology_max_die_per_package();

	ehfi_instances = kcalloc(max_ehfi_instances, sizeof(*ehfi_instances),
	                         GFP_KERNEL);
	if (!ehfi_instances)
		return -ENOMEM;


	ehfi_proc= proc_create("ehfi", 0666, pmc_dir, &fops);

	if (!ehfi_proc) {
		ret=-ENOMEM;
		goto err_nomem;
	}

	/* Activate EHFI Table for each socket */
	/* For now we activate the table just for this socket...
	 * TODO multiple sockets that require Launching defferred work
	 */
	if ((ret=setup_hw_table(&ehfi_instances[die_id])))
		goto err_nomem;

	/* Link each cpu instance */
	on_each_cpu(init_cpu_instance, NULL, 1);

	ehfi_features.nr_lp_entries=ehfi_features.active_lp_indexes=0;

	for (i=0; i<nr_cpus; i++) {
		info = &per_cpu(ehfi_cpu_info, i);
		printk(KERN_INFO "CPU %lu entry: index=%d addr= 0x%p\n",i, info->index, info->ehfi_instance->hw_table);

		if (!(ehfi_features.active_lp_indexes & (1<<info->index))) {
			ehfi_features.active_lp_indexes|=1<<info->index;
			ehfi_features.nr_lp_entries++;
		}
	}

	printk(KERN_INFO "*** EHCI IS SUPPORTED ***\n");
	printk(KERN_INFO "- EHFI capabilities\n");
	printk(KERN_INFO "\t* HW classes: %u\n\t* Performance cap: %u\n\t* Energy Efficiency cap: %u\n",
	       ehfi_features.nr_classes,
	       ehfi_features.capabilities.split.performance,
	       ehfi_features.capabilities.split.energy_efficiency);
	printk(KERN_INFO "- EHFI Table\n");
	printk(KERN_INFO "\t* Pages required size: %u\n\t* Header size: %u\n\t* Entry stride: %u\n\t* LP entries: %u\n",
	       ehfi_features.nr_table_pages,
	       ehfi_features.hdr_size,
	       ehfi_features.cpu_stride,
	       ehfi_features.nr_lp_entries);

	return 0;

err_nomem:

	if (ehfi_proc) {
		ehfi_proc=NULL;
		remove_proc_entry("ehfi", pmc_dir);
	}

	kfree(ehfi_instances);
	return ret;
}


void intel_ehfi_release(void)
{
	unsigned long cpu=smp_processor_id();
	u16 die_id = topology_logical_die_id(cpu);

	/* Unlink per-cpu instances */
	on_each_cpu(unlink_cpu_instance, NULL, 1);

	disable_hw_table(&ehfi_instances[die_id]);

	kfree(ehfi_instances);

}

void intel_ehfi_process_event()
{
	struct ehfi_instance *ehfi_instance;
	int cpu = smp_processor_id();
	struct ehfi_cpu_info *info;
	u64 new_timestamp;
	u64 msr_val;

	if (!this_cpu_has(X86_FEATURE_HFI)) {
		trace_printk("Thermal interrupt other unsup \n");
		return;
	}

	rdmsrl(MSR_IA32_PACKAGE_THERM_STATUS, msr_val);

	if (!(msr_val & PACKAGE_THERM_STATUS_EHFI_UPDATED)) {
		trace_printk("Thermal interrupt other no EHFI\n");
		return;
	}

	trace_printk("EHFI table changed\n");

	info = &per_cpu(ehfi_cpu_info, cpu);
	if (!info)
		return;

	/*
	 * It is possible that we get an HFI thermal interrupt on this CPU
	 * before its HFI instance is initialized. This is not a problem. The
	 * CPU that enabled the interrupt for this package will also get the
	 * interrupt and is fully initialized.
	 */
	ehfi_instance = info->ehfi_instance;
	if (!ehfi_instance)
		return;

	/* On most systems, all CPUs in the package receive a package-level
	 * thermal interrupt when there is an HFI update. It is sufficient to
	 * let a single CPU to acknowledge the update and schedule work to
	 * process it. The remaining CPUs can resume their work.
	 */
	if (!raw_spin_trylock(&ehfi_instance->event_lock))
		return;



	/* Skip duplicated updates. */
	new_timestamp = *(u64 *)ehfi_instance->hw_table;

	trace_printk("Timestamp %llx\n",new_timestamp);


	if (ehfi_instance->prev_timestamp == new_timestamp) {
		trace_printk("Same timestamp detected\n");
		raw_spin_unlock(&ehfi_instance->event_lock);
		return;
	}

	raw_spin_lock(&ehfi_instance->table_lock);

	ehfi_instance->prev_timestamp=new_timestamp;

	/* TODO COPY TABLE ... */
	raw_spin_unlock(&ehfi_instance->table_lock);
	raw_spin_unlock(&ehfi_instance->event_lock);


	/*
	 * Let hardware know that we are done reading the EHFI table and it is
	 * free to update it again.
	 */
	//msr_val &= THERM_STATUS_CLEAR_PKG_MASK & ~PACKAGE_THERM_STATUS_EHFI_UPDATED;

	msr_val&=THERM_STATUS_CLEAR_PKG_MASK;

	wrmsrl(PMCT_MSR_IA32_PACKAGE_THERM_STATUS, msr_val & ~PACKAGE_THERM_STATUS_EHFI_UPDATED);

	//schedule_delayed_work(&ehfi_instance->update_work, EHFI_UPDATE_INTERVAL);
}



