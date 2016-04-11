/*
 * core.c
 *
 ******************************************************************************
 *
 * Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 ******************************************************************************
 *
 *  2015-05-10  Modified by Abel Serrano to move the code of low-level routines 
 				defined previously in pmctrack.c (command-line tool). This was a major
 				code refactoring operation.
 *  2015-08-10  Modified by Juan Carlos Saez to include support for mnemonic-based
 *				event configurations and system-wide monitoring mode
 */
#include <pmctrack.h>
#include <pmctrack_internal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <wait.h>
#include <err.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/types.h>
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif


const char* pmc_enable_entry="/proc/pmc/enable";
const char* pmc_monitor_entry="/proc/pmc/monitor";
const char* pmc_config_entry="/proc/pmc/config";
const char* pmc_props_entry="/proc/pmc/properties";

static const char* sample_type_to_str[PMC_NR_SAMPLE_TYPES]= {"tick","ebs","exit","migration","self"};

/* 
 * Tell PMCTrack's kernel module which virtual counters 
 * must be monitored.
 */
int pmct_config_virtual_counters(const char* virtcfg, int syswide)
{
	int len=0;
	char buf[MAX_CONFIG_STRING_SIZE+1+9];
	int fd=open(pmc_config_entry, O_WRONLY);

	if(fd ==-1) {
		warnx("Can't open %s\n",pmc_config_entry);
		return -1;
	}

	if (syswide)
		len=sprintf(buf,"%s\n",virtcfg);
	else
		len=sprintf(buf,"selfcfg %s\n",virtcfg);
	len=write(fd,buf,len);

	if(len <= 0) {
		warnx("Write error in %s\n",pmc_config_entry);
		return -1;
	}

	close(fd);
	return 0;
}

/*
 * The function parses a null-terminated array of strings 
 * with PMC configurations in the raw format (userpmccfg), and 
 * returns the following information: (1) Number of PMCs in use
 * across experiments in the specified configuration, (2) bitmask that indicates
 * which PMCs are used across experiments, (3) flag (ebs) that indicates
 * if the EBS mode is requested in any of the configuration strings and
 * (4) number of experiments detected (# items in the userpmccfg vector)
 *
 */
int pmct_check_counter_config(const char* userpmccfg[],unsigned int* nr_counters,
                              unsigned int* counter_mask,
                              unsigned int* ebs,
                              unsigned int* nr_experiments)
{
	int fd;
	unsigned int pmcmask=0;
	unsigned int counters=0;
	int i=0;
	char* flag;
	int idx,val;
	char line[MAX_CONFIG_STRING_SIZE+1];
	char* strconfig=NULL;
	unsigned int nr_exps;
	int len;
	unsigned int ebs_on=0;

	if (userpmccfg && userpmccfg[0]!=NULL) {
		i=0;
		nr_exps=0;
		while (userpmccfg[i]!=NULL) {

			/* Copy config in temporary storage */
			strconfig=line;
			strncpy(strconfig,userpmccfg[i],MAX_CONFIG_STRING_SIZE+1);

			/* Parse configuration string to figure this out */
			while((flag = strsep(&strconfig, ","))!=NULL) {
				if((sscanf(flag,"pmc%i=%x", &idx, &val))>0) {
					/* If that counter was not marked previously */
					if ((pmcmask & (0x1<<idx))==0) {
						pmcmask|=(0x1<<idx);
						counters++;
					}
				} else if((sscanf(flag,"ebs%i=%x", &idx, &val))>0) {
					ebs_on=1;
				}
			}
			i++;
			nr_exps++;
		}

		/* Store in return values*/
		(*counter_mask)=pmcmask;
		(*nr_counters)=counters;
		(*ebs)=ebs_on;
		(*nr_experiments)=nr_exps;
	} else {
		/*
			$ echo get pmcmask > /proc/pmc/properties
			$ cat /proc/pmc/properties
			npmcmask=0x000000001b
			$ echo nr_pmcs > /proc/pmc/properties
			$ cat /proc/pmc/properties
			nr_pmcs=4
		*/
		fd=open(pmc_props_entry, O_RDWR);
		if(fd == -1) {
			warnx("Error de apertura de %s\n",pmc_props_entry);
			return -1;
		}

		/* Write */
		write(fd,"get nr_pmcs\n",strlen("get nr_pmcs\n"));

		/* Read nr_pmcs */
		if ((len=read(fd,line,MAX_CONFIG_STRING_SIZE))==-1) {
			warnx("Cant' read property from %s\n",pmc_props_entry);
			close(fd);
			return -1;
		}

		line[len]='\0';

		if (sscanf(line,"nr_pmcs=%d\n",&counters)!=1) {
			warnx("Cant' read property from %s\n",pmc_props_entry);
			close(fd);
			return -1;
		}

		/* Write */
		lseek(fd,0,SEEK_SET);
		write(fd,"get pmcmask\n",strlen("get pmcmask\n"));

		/* Read property */
		if ((len=read(fd,line,MAX_CONFIG_STRING_SIZE))==-1) {
			warnx("Cant' read property from %s\n",pmc_props_entry);
			close(fd);
			return -1;
		}

		line[len]='\0';

		if (sscanf(line,"pmcmask=0x%x\n",&pmcmask)!=1) {
			warnx("Cant' read property from %s\n",pmc_props_entry);
			close(fd);
			return -1;
		}

		/* Write */
		lseek(fd,0,SEEK_SET);
		write(fd,"get nr_experiments\n",strlen("get nr_experiments\n"));

		/* Read property */
		if ((len=read(fd,line,MAX_CONFIG_STRING_SIZE))==-1) {
			warnx("Cant' read property from %s\n",pmc_props_entry);
			close(fd);
			return -1;
		}

		line[len]='\0';

		/* Read property */
		if (sscanf(line,"nr_experiments=%d\n",&nr_exps)!=1) {
			warnx("Cant' read property from %s\n",pmc_props_entry);
			close(fd);
			return -1;
		}

		(*counter_mask)=pmcmask;
		(*nr_counters)=counters;
		(*ebs)=0;	/* Ebs is not supported in in kernel monitoring */
		(*nr_experiments)=nr_exps;
		close(fd);
	}
	return 0;
}

/*
 * Parse a string that specifies the virtual-counter configuration
 * in the raw format and return the following information: 
 * (1) Number of virtual counters specified in the string and 
 * (2) bitmask that indicates which virtual counters are used
 *
 */
int pmct_check_vcounter_config(const char* virtcfg,unsigned int* nr_virtual_counters,unsigned int* virtual_mask)
{
	int pmcmask=0;
	int counters=0;
	char* flag;
	int idx,val;
	char cpbuf[MAX_CONFIG_STRING_SIZE+1];
	char* strconfig=NULL;

	if (virtcfg) {

		/* Copy config in temporary storage */
		strconfig=cpbuf;
		strncpy(strconfig,virtcfg,MAX_CONFIG_STRING_SIZE+1);

		/* Parse configuration string to figure this out */
		while((flag = strsep(&strconfig, ","))!=NULL) {
			if((sscanf(flag,"virt%i=%x", &idx, &val))>0) {
				/* If that counter was not marked previously */
				if ((pmcmask & (0x1<<idx))==0) {
					pmcmask|=(0x1<<idx);
					counters++;
				}
			}
		}

		/* Store in return values*/
		(*virtual_mask)=pmcmask;
		(*nr_virtual_counters)=counters;
	}
	return 0;
}

/* 
 * Setup timeout for TBS or scheduler-driven monitoring mode (specified in ms).
 * If the kernel forced the PMC configuration a non-zero value should be specified
 * as the "kernel_control" parameter
 */
int pmct_config_timeout(int msecs, int kernel_control)
{
	int len=0;
	char buf[MAX_CONFIG_STRING_SIZE];
	int fd=open(pmc_config_entry, O_WRONLY);
	char *key=NULL;

	if (kernel_control) 
		key="sched_sampling_period_t";
	else
		key="timeout";

	if(fd ==-1) {
		warnx("Can't open %s\n",pmc_config_entry);
		return -1;
	}

	len=sprintf(buf,"%s %d\n",key,msecs);
	len=write(fd,buf,len);

	if(len <= 0) {
		warnx("Write error in %s\n",pmc_config_entry);
		return -1;
	}

	close(fd);
	return 0;
}

/* 
 * Tell PMCTrack's kernel module which PMC events 
 * must be monitored.
 */
int pmct_config_counters(const char* strcfg[], int syswide)
{
	int len=0;
	char buf[MAX_CONFIG_STRING_SIZE+1+9];
	int i=0;
	int fd=open(pmc_config_entry, O_WRONLY);

	if(fd ==-1) {
		warnx("Can't open %s\n",pmc_config_entry);
		return -1;
	}

	/* Write */
	while (strcfg[i]!=NULL) {

		if (syswide)
			len=sprintf(buf,"%s\n",strcfg[i]);
		else
			len=sprintf(buf,"selfcfg %s\n",strcfg[i]);
		len=write(fd,buf,len);

		if(len < 0) {
			warnx("Can't write in %s\n",pmc_config_entry);
			return -1;
		} else if(len == 0) {
			warnx("Can't configure counters");
			return -1;
		}
		i++;
		/* Rewind write pointer */
		lseek(fd,0,SEEK_SET);
	}

	close(fd);
	return 0;
}

/*
 * Tell PMCTrack's kernel module to start a monitoring session
 * in per-thread mode 
 */
int pmct_start_counting( void )
{
	int fd;
	fd = open(pmc_enable_entry, O_WRONLY);
	if(fd == -1) {
		warnx("Can't open  %s\n",pmc_enable_entry);
		return -1;
	}
	if(write(fd, "ON", 2) < 0) {
		warnx("Can't write in %s\n",pmc_enable_entry);
		return -1;
	}
	close(fd);

	return 0;
}

/*
 * Tell PMCTrack's kernel module to start a monitoring session
 * in system-wide mode 
 */
int pmct_syswide_start_counting( void )
{
	int fd;
	fd = open(pmc_enable_entry, O_WRONLY);
	if(fd == -1) {
		warnx("Can't open  %s\n",pmc_enable_entry);
		return -1;
	}
	if(write(fd, "syswide on", 10) < 0) {
		warnx("Can't write in %s\n",pmc_enable_entry);
		return -1;
	}
	close(fd);

	return 0;
}


/* 
 * Print a header in the "normalized" format for a table of 
 * PMC and virtual-counter samples
 */
void pmct_print_header (FILE* fo, unsigned int nr_experiments,
                        unsigned int pmcmask,
                        unsigned int virtual_mask,
                        int extended_output,
                        int syswide)
{
	int i;
	char* str[2]= {"pid","cpu"};
	int index=syswide?1:0; /* To make sure it is in the allowed range */

	if (!extended_output && nr_experiments<2) {
		/* Legacy mode */
		fprintf(fo, "%7s %6s %10s", "nsample", str[index], "event");
	} else {
		/* General mode */
		fprintf(fo, "%7s %6s %8s %5s %10s", "nsample", str[index], "coretype","expid","event");
	}

	for(i=0; i<MAX_PERFORMANCE_COUNTERS; i++) {
		if(pmcmask & (0x1<<i)) {
			fprintf(fo, " %12s%i","pmc",i);
		}
	}

	for(i=0; (virtual_mask) && (i<MAX_VIRTUAL_COUNTERS); i++) {
		if(virtual_mask & (0x1<<i)) {
			fprintf(fo, " %12s%i","virt",i);
		}
	}

	fprintf(fo, "\n");

}

/* 
 * Print a sample row in the "normalized" format for a table of 
 * PMC and virtual-counter samples
 */
void pmct_print_sample (FILE* fo, unsigned int nr_experiments,
                        unsigned int pmcmask,
                        unsigned int virtual_mask,
                        unsigned int extended_output,
                        int nsample,
                        pmc_sample_t* sample)
{

	char line_out[256]; /* Allocating memory for output */
	char* dst=line_out;
	int j,cnt=0;
	unsigned int remaining_pmcmask=pmcmask;
	
	/* Max supported counters... */
	for(j=0; (j<MAX_PERFORMANCE_COUNTERS) && (remaining_pmcmask); j++) {
		if(sample->pmc_mask & (0x1<<j)) {
#if defined(__i386__) || defined(__arm__)
			dst+=sprintf(dst,"%13llu ",sample->pmc_counts[cnt++]);
#else
			dst+=sprintf(dst,"%13lu ",sample->pmc_counts[cnt++]);
#endif
			remaining_pmcmask&=~(0x1<<j);
		}
		/* Print dash only if the particular pmcmask contains the pmc*/
		else if (remaining_pmcmask & (0x1<<j)) {
			dst+=sprintf(dst,"%13s ","-");
		}
	}

	remaining_pmcmask=virtual_mask;
	cnt=0;
	for(j=0; (j<MAX_VIRTUAL_COUNTERS) && (remaining_pmcmask) ; j++) {
		if(sample->virt_mask & (0x1<<j)) {
#if defined(__i386__) || defined(__arm__)
			dst+=sprintf(dst,"%13llu ",sample->virtual_counts[cnt++]);
#else
			dst+=sprintf(dst,"%13lu ",sample->virtual_counts[cnt++]);
#endif
			remaining_pmcmask&=~(0x1<<j);
		} else if (remaining_pmcmask & (0x1<<j)) {
			dst+=sprintf(dst,"%13s ","-");
		}
	}

	if (!extended_output && nr_experiments<2) {
		/* Legacy mode */
		fprintf(fo, "%7d %6d %10s %s\n", nsample, sample->pid, sample_type_to_str[sample->type], line_out);
	} else {
		fprintf(fo, "%7d %6d %8d %5d %10s %s\n", nsample,sample->pid, sample->coretype,sample->exp_idx, sample_type_to_str[sample->type], line_out);
	}
}

/* 
 * Accumulate PMC and virtual-counter values from one sample into 
 * another sample.
 * (This function is used to implement the "-A" option 
 * of the pmctrack command-line tool)
 */
void pmct_accumulate_sample (unsigned int nr_experiments,
                             unsigned int pmcmask,
                             unsigned int virtual_mask,
                             unsigned char copy_metainfo,
                             pmc_sample_t* sample,
                             pmc_sample_t* accum)
{
	int j,cnt=0;
	unsigned int remaining_pmcmask=pmcmask;

	if (copy_metainfo) {
		accum->type=sample->type;
		accum->coretype=sample->coretype;
		accum->exp_idx=sample->exp_idx;
		accum->pid=sample->pid;
		accum->pmc_mask=sample->pmc_mask;
		accum->nr_counts=sample->nr_counts;
		accum->virt_mask=sample->virt_mask;
		accum->nr_virt_counts=sample->nr_virt_counts;
	}

	/* Max supported counters... */
	for(j=0; (j<MAX_PERFORMANCE_COUNTERS) && (remaining_pmcmask); j++) {
		if(sample->pmc_mask & (0x1<<j)) {
			accum->pmc_counts[cnt]+=sample->pmc_counts[cnt]*nr_experiments; /* Multiplex info */
			remaining_pmcmask&=~(0x1<<j);
			cnt++;
		}
	}

	remaining_pmcmask=virtual_mask;
	cnt=0;
	for(j=0; (j<MAX_VIRTUAL_COUNTERS) && (remaining_pmcmask) ; j++) {
		if(sample->virt_mask & (0x1<<j)) {
			accum->virtual_counts[cnt]+=sample->virtual_counts[cnt]; /* Do not multiply virt counts */
			remaining_pmcmask&=~(0x1<<j);
			cnt++;
		}
	}

}

/*
 * Obtain a file descriptor of the special file exported by
 * PMCTrack's kernel module file to retrieve performance samples 
 */
int pmct_open_monitor_entry(void)
{
	int fd = open(pmc_monitor_entry, O_RDWR);
	if(fd == -1) {
		warnx("can't open %s\n",pmc_monitor_entry);
	}
	return fd;
}

/* 
 * Become the monitor process of another process with PID=pid.
 * Upon invocation to this function the monitor process will
 * be able to retrieve PMC and/or virtual-counter samples
 * using a special file exported by PMCTrack's kernel module
 *
 * If config_pmcs !=0, the attached process will inherit PMC and
 * virtual counter configuration from the parent process
 */
int pmct_attach_process (pid_t pid, int config_pmcs)
{
	char str[30];
	int siz;

	int fd = open(pmc_monitor_entry, O_WRONLY);
	if(fd == -1) {
		warnx("can't open %s\n",pmc_monitor_entry);
		return -1;
	}
	if (config_pmcs)
		siz=sprintf(str, "pid_attach %d", pid);
 	else
		siz=sprintf(str, "pid_monitor %d", pid);

	if(write(fd, str, siz+1) < 0) 
		return -1;
	
	close(fd);
	return 0;
}

/*
 * Detach process from monitor 
 */
int pmct_detach_process (pid_t pid){
	char str[30];
	int siz;

	int fd = open(pmc_monitor_entry, O_WRONLY);
	if(fd == -1) {
		warnx("can't open %s\n",pmc_monitor_entry);
		return -1;
	}
	siz=sprintf(str, "pid_detach %d", pid);

	if(write(fd, str, siz+1) < 0) 
		return -1;
	
	close(fd);
	return 0;
}

/*
 * Retrieve performance samples from the special file exported by
 * PMCTrack's kernel module
 */
int pmct_read_samples (int fd, pmc_sample_t* samples, int max_samples)
{
	int nr_samples = 0;
	int nbytes = 0;
	int max_buffer_size=sizeof(pmc_sample_t)*max_samples;

	if((nbytes = read(fd, samples, max_buffer_size)) < 0) {
		if (errno!=EINTR)
			warnx("Can't read from %s\n",pmc_monitor_entry);
		return -1;
	}

	/* Reset read counter */
	lseek(fd, 0, SEEK_SET);

	nr_samples=nbytes/sizeof(pmc_sample_t);
	return nr_samples;
}

/*
 * Initialize and return a PMCTrack descriptor after establishing a "connection" with
 * the kernel module.
 */
pmctrack_desc_t* pmctrack_init(unsigned int max_nr_samples)
{

	pmctrack_desc_t* desc=malloc(sizeof(pmctrack_desc_t));

	if (!desc)
		return NULL;

	/* Initialize fields*/
	desc->fd_monitor=-1;
	desc->pmcmask=0;
	desc->kern_pmcmask=0;
	desc->nr_pmcs=0;
	desc->nr_virtual_counters=0;
	desc->virtual_mask=0;
	desc->nr_experiments=0;
	desc->ebs_on=0;
	desc->nr_samples=0;
	desc->flags=0;
	memset(desc->event_mapping,0,sizeof(counter_mapping_t)*MAX_PERFORMANCE_COUNTERS);
	desc->global_pmcmask=0;

	/* Check kernel-imposed config */
	if (pmct_check_counter_config(NULL,&desc->nr_pmcs,&desc->kern_pmcmask,
	                              &desc->ebs_on,&desc->nr_experiments)) {
		free(desc);
		return NULL;
	}
	/* Copy mask if the kernel controls the counters */
	if (desc->kern_pmcmask)
		desc->pmcmask=desc->kern_pmcmask;

	/* Open monitor file */
	if ((desc->fd_monitor=open(pmc_monitor_entry,O_RDWR))==-1) {
		warnx("Error opening %s\n entry",pmc_monitor_entry);
		free(desc);
		return NULL;
	}

	if (max_nr_samples<=0) {
		desc->flags|=PMCT_FLAG_SHARED_REGION;
		/* Request shared memory region */
		if ((desc->samples=pmct_request_shared_memory_region(desc->fd_monitor,&desc->max_nr_samples))==NULL) {
			warnx("Can't map shared memory region\n");
			free(desc);
			return NULL;
		}
	} else {
		if (pmct_set_kernel_buffer_size(sizeof(pmc_sample_t)*max_nr_samples)) {
			warnx("Can't set up the desired kernel buffer size\n");
			free(desc);
			return NULL;
		}

		/* Allocate memory from the heap instead */
		desc->samples=malloc(sizeof(pmc_sample_t)*max_nr_samples);
		desc->max_nr_samples=max_nr_samples;
		if (!desc->samples) {
			warnx("Can't allocate memory for the buffer\n");
			free(desc);
			return NULL;
		}
		memset(desc->samples,0,sizeof(pmc_sample_t)*desc->max_nr_samples);
	}

	return desc;
}

/* Free up descriptor */
int pmctrack_destroy(pmctrack_desc_t* desc)
{
	if (!desc)
		return -1;

	if (desc->fd_monitor!=-1) {
		close(desc->fd_monitor);
		desc->fd_monitor=-1;
	}

	if (desc->samples && !(desc->flags & PMCT_FLAG_SHARED_REGION)) {
		free(desc->samples);
		desc->samples=NULL;
	}
	free(desc);
	return 0;
}

/*
 * This function makes it possible to create a copy of a descriptor
 * for another thread in the application. It is more efficient to clone
 * a descriptor than to create a new one with pmctrack_init()
 */
pmctrack_desc_t* pmctrack_clone_descriptor(pmctrack_desc_t* orig)
{

	pmctrack_desc_t* dest;

	if (!orig)
		return NULL;

	dest=malloc(sizeof(pmctrack_desc_t));

	if (!dest)
		return NULL;


	dest->fd_monitor=-1;
	dest->pmcmask=orig->pmcmask;
	dest->kern_pmcmask=orig->kern_pmcmask;
	dest->nr_pmcs=orig->nr_pmcs;
	dest->nr_virtual_counters=orig->nr_virtual_counters;
	dest->virtual_mask=orig->virtual_mask;
	dest->nr_experiments=orig->nr_experiments;
	dest->ebs_on=0;
	dest->nr_samples=0;
	dest->flags=orig->flags;
	/* Fields to build metainfo header */
	memcpy(dest->event_mapping,orig->event_mapping,sizeof(counter_mapping_t)*MAX_PERFORMANCE_COUNTERS);
	dest->global_pmcmask=orig->global_pmcmask;

	/* Open monitor file */
	if ((dest->fd_monitor=open(pmc_monitor_entry,O_RDWR))==-1) {
		warnx("Error opening %s\n entry",pmc_monitor_entry);
		free(dest);
		return NULL;
	}

	if (dest->flags & PMCT_FLAG_SHARED_REGION) {
		/* Request shared memory region */
		if ((dest->samples=pmct_request_shared_memory_region(dest->fd_monitor,&dest->max_nr_samples))==NULL) {
			warnx("Can't map shared memory region\n");
			free(dest);
			return NULL;
		}
	} else {
		/* Allocate memory from the heap instead */
		dest->samples=malloc(sizeof(pmc_sample_t)*orig->max_nr_samples);
		dest->max_nr_samples=orig->max_nr_samples;
		if (!dest->samples) {
			warnx("Can't allocate memory for the buffer\n");
			free(dest);
			return NULL;
		}
		memset(dest->samples,0,sizeof(pmc_sample_t)*dest->max_nr_samples);
	}

	return dest;
}


static int __pmctrack_config_counters(pmctrack_desc_t* desc, const char* strcfg[], const char* virtcfg, int mux_timeout_ms)
{

	if (mux_timeout_ms==0)
		mux_timeout_ms=300000;

	/**/
	if (mux_timeout_ms!=0 && pmct_config_timeout(mux_timeout_ms,desc->kern_pmcmask))
		return -1;

	/* Configure counters if there is something to configure */
	if (strcfg && strcfg[0]) {
		/* Parse counter config */
		pmct_check_counter_config(strcfg,&desc->nr_pmcs,&desc->pmcmask,
		                          &desc->ebs_on,&desc->nr_experiments);

		/* Tell the kernel what we want to count */
		if (pmct_config_counters(strcfg,0))
			return -1;

	}

	/* Configure virtual counters */
	if (virtcfg) {
		pmct_check_vcounter_config(virtcfg,&desc->nr_virtual_counters,&desc->virtual_mask);
		if (pmct_config_virtual_counters(virtcfg,0))
			return -1;
	}

	return 0;
}

/*
 * Tell PMCTrack's kernel module the desired PMC and virtual counter
 * configuration. The configuration must be specified using the raw format
 * (the only one that the kernel "understands") for both PMC and virtual counters.
 */
int pmctrack_config_counters(pmctrack_desc_t* desc, const char* strcfg[], const char* virtcfg, int mux_timeout_ms)
{
	int ret=__pmctrack_config_counters(desc,strcfg,virtcfg,mux_timeout_ms);
	if (ret==0)
		desc->flags|=PMCT_FLAG_RAW_PMC_CONFIG;
	return ret;
}

/*
 * Tell PMCTrack's kernel module the desired PMC and virtual counter
 * configuration. The configuration must be specified using the default format
 * used by the pmctrack command-line tool, which accepts event mnemonics for both
 * PMC and virtual counters.
 */
int pmctrack_config_counters_mnemonic(pmctrack_desc_t* desc, const char* user_strcfg[], const char* virtcfg, int mux_timeout_ms, int pmu_id)
{
	int ret=0;
	int i=0;
	unsigned int nr_experiments;
	char* strcfg[MAX_RAW_COUNTER_CONFIGS_SAFE];
	char* raw_virtcfg=NULL;
	int vmnemonics=0;

	/* Initialization for a safer memory release later */
	for(i=0; i<MAX_RAW_COUNTER_CONFIGS_SAFE; i++)
		strcfg[i]=NULL;

	if ((ret=pmct_parse_pmc_configuration((const char**)user_strcfg,
	                                      0,
	                                      pmu_id,
	                                      strcfg,
	                                      &nr_experiments,
	                                      &desc->global_pmcmask,
	                                      desc->event_mapping))) {
		warnx("Error parsing event mnemonics");
		goto free_up_strcfg;
	}

	/* Check vcounter configuration */
	if (virtcfg) {
		if (pmct_parse_vcounter_config(virtcfg,
		                               &desc->virtual_mask,
		                               &desc->nr_virtual_counters,
		                               &vmnemonics,
		                               &raw_virtcfg))
			return -1;

		if (vmnemonics)
			desc->flags|= PMCT_FLAG_VIRT_COUNTER_MNEMONICS;
	}

	if ((ret=__pmctrack_config_counters(desc,
	                                    (const char**)strcfg,
	                                    raw_virtcfg,
	                                    mux_timeout_ms)))
		goto free_up_strcfg;


	desc->flags&=~PMCT_FLAG_RAW_PMC_CONFIG;
free_up_strcfg:
	free(raw_virtcfg);
	for(i=0; strcfg[i]!=NULL; i++)
		free(strcfg[i]);

	return ret;
}

static inline int pmct_start_counters_gen(pmctrack_desc_t* desc,int syswide)
{
	char* key[2]= {"ON","syswide on"};
	int index=syswide?1:0; /* To make sure it is in the allowed range */

	if(write(desc->fd_monitor,key[index], strlen(key[index])+1) < 0) {
		warnx("Write error in %s\n",pmc_monitor_entry);
		return -1;
	}

	if (syswide)
		desc->flags|=PMCT_FLAG_SYSWIDE;
	else
		desc->flags&=~PMCT_FLAG_SYSWIDE;
	return 0;
}

static inline int pmct_stop_counters_gen(pmctrack_desc_t* desc, int syswide)
{
	char* key[2]= {"OFF","syswide off"};
	int index=syswide?1:0; /* To make sure it is in the allowed range */
	int nbytes=0;

	/* Disable self monitoring */
	if(write(desc->fd_monitor,key[index], strlen(key[index])+1) < 0) {
		warnx("Write error in  %s:%s\n",pmc_monitor_entry,strerror(errno));
		return -1;
	}
	/* Read stuff */
	if((nbytes = read(desc->fd_monitor, desc->samples, sizeof(pmc_sample_t)*desc->max_nr_samples)) < 0) {
		perror("Read error in /proc/pmc/monitor\n");
		return -1;
	}

	desc->nr_samples=nbytes/sizeof(pmc_sample_t);

	return 0;
}


/*
 * Start a monitoring session in per-thread mode. Note that PMC and/or
 * virtual counter configurations must have been specified beforehand.
 */
int pmctrack_start_counters(pmctrack_desc_t* desc)
{
	return pmct_start_counters_gen(desc,0);
}

/*
 * Stops a monitoring session in per-thread mode. This function also
 * retrieves PMC and virtual counter samples collected by the kernel.
 * Samples are stored in the PMCTrack descriptor, and can be accessed
 * after invoking this function.
 */
int pmctrack_stop_counters(pmctrack_desc_t* desc)
{
	return pmct_stop_counters_gen(desc,0);
}

/*
 * Start a monitoring session in system-wide mode. Note that PMC and/or
 * virtual counter configurations must have been specified beforehand.
 */
int pmctrack_start_counters_syswide(pmctrack_desc_t* desc)
{
	return pmct_start_counters_gen(desc,1);
}

/*
 * Stops a monitoring session in system-wide mode. This function also
 * retrieves PMC and virtual counter samples collected by the kernel.
 * Samples are stored in the PMCTrack descriptor, and can be accessed
 * after invoking this function.
 */
int pmctrack_stop_counters_syswide(pmctrack_desc_t* desc)
{
	return pmct_stop_counters_gen(desc,1);
}

/*
 * Prints a summary with all the monitoring information collected
 * in the last per-thread or system-wide monitoring session.
 */
void pmctrack_print_counts(pmctrack_desc_t* desc, FILE* fo, int extended_output)
{

	int i=0;
	pmc_sample_t* cur;
	int syswide=desc->flags & PMCT_FLAG_SYSWIDE;

	// print event-to-counter mappings
	if (!(desc->flags & PMCT_FLAG_RAW_PMC_CONFIG)) {
		fprintf(fo,"[Event-to-counter mappings]\n");
		pmct_print_counter_mappings(fo,desc->event_mapping,
		                            desc->global_pmcmask,
		                            desc->nr_experiments);

		if (desc->flags & PMCT_FLAG_VIRT_COUNTER_MNEMONICS)
			pmct_print_selected_virtual_counters(fo,desc->virtual_mask);

		fprintf(fo,"[Event counts]\n");
	}

	// printing header
	pmct_print_header(fo,desc->nr_experiments,desc->pmcmask,desc->virtual_mask,extended_output,syswide);

	// printing samples
	for (i=0; i<desc->nr_samples; i++) {
		cur = &desc->samples[i];

		pmct_print_sample (fo,desc->nr_experiments, desc->pmcmask, desc->virtual_mask, extended_output, i+1, cur);
	}
}

/*
 * Returns an array of samples collected after stopping a monitoring session.
 * The number of samples in the array is stored in the nr_samples output parameter.
 */
pmc_sample_t* pmctrack_get_samples(pmctrack_desc_t* desc,int* nr_samples)
{
	*nr_samples=desc->nr_samples;
	return desc->samples;
}

/*
 * Set up the size of the kernel buffer used to store PMC and virtual 
 * counter values 
 */
int pmct_set_kernel_buffer_size(unsigned int nr_bytes)
{
	int len=0;
	char buf[128];
	int fd=open(pmc_config_entry, O_WRONLY);

	if(fd ==-1) {
		warnx("Can't open %s\n",pmc_config_entry);
		return -1;
	}

	len=sprintf(buf,"kernel_buffer_size_t %u\n",nr_bytes);
	len=write(fd,buf,len);

	if(len <= 0) {
		warnx("Write error in %s\n",pmc_config_entry);
		return -1;
	}

	close(fd);
	return 0;
}

/*
 * Request a memory region shared between kernel and user space to
 * enable efficient communication between the monitor process and 
 * PMCTrack's kernel module when retrieving performance samples.
 */
pmc_sample_t* pmct_request_shared_memory_region(int monitor_fd, unsigned int* max_samples)
{
	pmc_sample_t* buf=(pmc_sample_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, monitor_fd, 0);
	if (buf == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	//printf("Mmap Ok. Address:%p\n",buf);
	(*max_samples)=PAGE_SIZE/sizeof(pmc_sample_t);
	return buf;
}

/*
 * Retrieve the event-to-PMC mapping information. The function should be used
 * only if pmctrack_config_counters_mnemonic() was used to configure performance
 * counters.
 */
int pmctrack_get_event_mapping( pmctrack_desc_t* desc,
                                counter_mapping_t* mapping,
                                unsigned int* nr_experiments,
                                unsigned int* used_pmcmask)
{

	/* Mnemonics were not used */
	if (desc->flags & PMCT_FLAG_RAW_PMC_CONFIG)
		return 1;

	(*nr_experiments)=desc->nr_experiments;
	(*used_pmcmask)=desc->global_pmcmask;
	memcpy(mapping,desc->event_mapping,sizeof(counter_mapping_t)*MAX_PERFORMANCE_COUNTERS);

	return 0;
}
