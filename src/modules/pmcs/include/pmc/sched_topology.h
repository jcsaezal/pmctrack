#ifndef SCHED_TOPOLOGY_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/smp.h>

#define MAX_CPUS_GROUP 64
#define MAX_GROUPS_SOCKET 16
#define MAX_SOCKETS_PLATFORM 4
#define MAX_GROUPS_PLATFORM (MAX_GROUPS_SOCKET*MAX_SOCKETS_PLATFORM)

typedef struct cpu_group {
	cpumask_t shared_cpu_map; /* CPUMask associated with this cpu group */
	cpumask_t online_cpu_map; /* CPUMask associated with this cpu group */
	int cpus[MAX_CPUS_GROUP];
	int nr_cpus;
	int nr_online_cpus;
	int llc_id;
	int group_id; /* For now llc_id==group_id, but the idea is that granularity should be configurable */
	int socket_id;
	int cpu_type; /* For potential handling of Big and Small core clusters */
	/* TODO add Simultaneous Multi-threading fields for SMT-aware schedulers */
	spinlock_t lock; /* For dynamic update of topology fields */
} cpu_group_t;

typedef struct cpu_socket {
	cpu_group_t* cpu_groups[MAX_GROUPS_SOCKET];
	int nr_cpu_groups;
	int socket_id;
} cpu_socket_t;



/* To update topology structure dynamically */
void cpu_group_on_cpu_off(int cpu);
void cpu_group_on_cpu_on(int cpu);
void populate_topology_structures(void);
cpu_group_t* get_cpu_group(int cpu);
int get_group_id_cpu(int cpu);
int get_llc_id_cpu(int cpu);
/* Returns all CPU Groups*/
cpu_group_t* get_platform_cpu_groups(int *nr_cpu_groups);
cpu_socket_t* get_platform_cpu_sockets(int *nr_cpu_sockets);


static inline cpu_group_t* get_cur_group(void)
{
	return get_cpu_group(smp_processor_id());
}


#endif