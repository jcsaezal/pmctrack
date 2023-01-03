#ifndef PMC_INTEL_PERF_METRICS
#define PMC_INTEL_PERF_METRICS
#include <pmc/pmu_config.h>


enum tma_metrics {
	RETIRING_TMA=0,
	BAD_SPECULATION_TMA,
	FRONTEND_BOUND_TMA,
	BACKEND_BOUND_TMA,
	HEAVY_OPS_TMA,
	BRANCH_MISP_TMA,
	FETCH_LATENCY_TMA,
	MEMORY_BOUND_TMA,
	NR_TMA_METRICS
};

typedef struct {
	uint64_t metrics;
	uint64_t slots;
	unsigned char tma_metrics[NR_TMA_METRICS];
} intel_perf_metric_ctl_t;


static inline void init_perf_metric_ctl(intel_perf_metric_ctl_t* ctl)
{
	ctl->metrics=0;
	ctl->slots=0;
}

#if defined(CONFIG_PMC_PERF_X86) || defined(CONFIG_PMC_CORE_I7)

int probe_intel_perf_metrics(void);
void save_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu);
void restore_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu);
void reset_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu);
void read_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu);
void measure_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu);

#else

static inline int probe_intel_perf_metrics(void)
{
	return -ENOTSUPP;
}
static inline void save_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu) {}
static inline void restore_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu) {}
static inline void reset_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu) {}
static inline void read_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu) {}
static inline void measure_intel_perf_metrics(intel_perf_metric_ctl_t* ctl, int cpu) {}

#endif
#endif
