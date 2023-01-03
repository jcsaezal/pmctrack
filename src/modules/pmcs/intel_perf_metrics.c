#include <pmc/intel_perf_metrics.h>
#include <pmc/pmu_config.h>
#include <linux/cpu.h>

#define PMCT_MSR_CORE_PERF_GLOBAL_STATUS	0x0000038e
#define PMCT_MSR_CORE_PERF_GLOBAL_CTRL	0x0000038f
#define PMCT_MSR_CORE_PERF_GLOBAL_OVF_CTRL	0x00000390
#define PMCT_GLOBAL_CTRL_EN_PERF_METRICS 48
#define PMCT_MSR_PERF_METRICS 0x00000329
#define PMCT_MSR_CORE_PERF_FIXED_CTR3	0x0000030c
#define PMCT_INTEL_PMC_FIXED_RDPMC_BASE		(1 << 30)
#define PMCT_INTEL_PMC_FIXED_RDPMC_METRICS		(1 << 29)
#define PMCT_INTEL_PMC_IDX_FIXED				       32
#define PMCT_INTEL_PMC_IDX_FIXED_SLOTS	(PMCT_INTEL_PMC_IDX_FIXED + 3)
#define PMCT_INTEL_PMC_MSK_FIXED_SLOTS	(1ULL << PMCT_INTEL_PMC_IDX_FIXED_SLOTS)
#define PMCT_INTEL_PMC_MSK_PERF_METRICS	(1ULL << PMCT_GLOBAL_CTRL_EN_PERF_METRICS)

static inline int is_big_core(int cpu)
{
	pmu_props_t* props=get_pmu_props_cpu(cpu);

	if (props->processor_model==151 || props->processor_model==152) /* Alder Lake */
		return props->coretype==1;
	else
		return 1;
}


int probe_intel_perf_metrics(void)
{

	pmu_props_t* props=get_pmu_props_cpu(smp_processor_id());

	if (props->processor_model==151 /* Alder Lake */ ||
	    props->processor_model==152 /* Alder Lake */ ||
	    props->processor_model==106 /* Ice Lake */ ||
	    props->processor_model==108  /* Ice Lake */
	   )
		return 0;
	else
		return -ENOTSUPP;

}

void save_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu)
{
	uint64_t val;

	if (!is_big_core(cpu))
		return;

	/* Disable and clear both counters */
	rdmsrl(PMCT_MSR_CORE_PERF_GLOBAL_CTRL,val);
	wrmsrl(PMCT_MSR_CORE_PERF_GLOBAL_CTRL,
	       val & ~(PMCT_INTEL_PMC_MSK_PERF_METRICS|PMCT_INTEL_PMC_MSK_FIXED_SLOTS));

	/* Backup values */
	rdmsrl(PMCT_MSR_CORE_PERF_FIXED_CTR3, ctl->slots);
	rdmsrl(PMCT_MSR_PERF_METRICS, ctl->metrics);

	/* Clear counter... */
	wrmsrl(PMCT_MSR_CORE_PERF_FIXED_CTR3, 0);
	wrmsrl(PMCT_MSR_PERF_METRICS, 0);
}

void restore_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu)
{
	uint64_t val;

	if (!is_big_core(cpu))
		return;

	/* Write and enable counters */
	wrmsrl(PMCT_MSR_CORE_PERF_FIXED_CTR3, ctl->slots);
	wrmsrl(PMCT_MSR_PERF_METRICS, ctl->metrics);

	rdmsrl(PMCT_MSR_CORE_PERF_GLOBAL_CTRL,val);
	wrmsrl(PMCT_MSR_CORE_PERF_GLOBAL_CTRL,
	       val | PMCT_INTEL_PMC_MSK_PERF_METRICS | PMCT_INTEL_PMC_MSK_FIXED_SLOTS );

	/**
	 * 		if (pmu->intel_cap.perf_metrics) {
				pmu->intel_ctrl |= 1ULL << GLOBAL_CTRL_EN_PERF_METRICS;
				pmu->intel_ctrl |= INTEL_PMC_MSK_FIXED_SLOTS;
			}
			...
				wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL,
		       intel_ctrl & ~cpuc->intel_ctrl_guest_mask);
	 */
}

void reset_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu)
{

	if (!is_big_core(cpu))
		return;

	wrmsrl(PMCT_MSR_CORE_PERF_FIXED_CTR3, 0);
	wrmsrl(PMCT_MSR_PERF_METRICS, 0);

	ctl->slots=0;
	ctl->metrics=0;
}


void read_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu)
{

	if (!is_big_core(cpu))
		return;

	rdpmcl((3 | PMCT_INTEL_PMC_FIXED_RDPMC_BASE), ctl->slots);
	rdpmcl(PMCT_INTEL_PMC_FIXED_RDPMC_METRICS, ctl->metrics);
}


void measure_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu)
{

	if (!is_big_core(cpu))
		return;

	rdpmcl((3 | PMCT_INTEL_PMC_FIXED_RDPMC_BASE), ctl->slots);
	rdpmcl(PMCT_INTEL_PMC_FIXED_RDPMC_METRICS, ctl->metrics);

	ctl->tma_metrics[RETIRING_TMA]=ctl->metrics & 0xff;
	ctl->tma_metrics[BAD_SPECULATION_TMA]=(ctl->metrics>>8) & 0xff;
	ctl->tma_metrics[FRONTEND_BOUND_TMA]=(ctl->metrics>>16) & 0xff;
	ctl->tma_metrics[BACKEND_BOUND_TMA]=(ctl->metrics>>24) & 0xff;
	ctl->tma_metrics[HEAVY_OPS_TMA]=(ctl->metrics>>32) & 0xff;
	ctl->tma_metrics[BRANCH_MISP_TMA]=(ctl->metrics>>40) & 0xff;
	ctl->tma_metrics[FETCH_LATENCY_TMA]=(ctl->metrics>>48) & 0xff;
	ctl->tma_metrics[MEMORY_BOUND_TMA]=(ctl->metrics>>56) & 0xff;
}
