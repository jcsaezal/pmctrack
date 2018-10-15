/*
 *  include/pmc/core2duo/pmc_bit_layout.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef CORE2DUO_PMC_BIT_LAYOUT_H
#define CORE2DUO_PMC_BIT_LAYOUT_H


#include <pmc/data_str/bit_field.h>
#include <pmc/core2duo/pmc_const.h>


#define clear_bit_layout(bl) (bl).m_value=0

/* PERFEVTSEL_MSR bit layout */
typedef struct {
	/* Enumeration of bitfields*/
	bit_field_64 m_evtsel; 	/* Event Selection */
	bit_field_64 m_umask; 	/* Unit Mask (Subevent selection) */
	bit_field_64 m_usr;  	/* User mode count (privilege levels 1,2 or 3) */
	bit_field_64 m_os;  	/* Kernel mode count (privilege level 0) */
	bit_field_64 m_e;   	/* Edge detection */
	bit_field_64 m_pc;  	/* Pin control */
	bit_field_64 m_int; 	/* APIC interrupt enable */
	bit_field_64 m_any; 	/* ANY thread field (HT) */
	bit_field_64 m_en;  	/* Enable counters */
	bit_field_64 m_inv; 	/* Invert flag */
	bit_field_64 m_cmask; 	/* CMASK */
	bit_field_64 m_nu2; 	/* Reserved */
	uint64_t m_value; 	/* 64-bit store */
}
evtsel_msr;


static inline void init_evtsel_msr ( evtsel_msr* evtsel )
{
	/* Bit layout initialization */
	init_bit_field ( &evtsel->m_evtsel,&evtsel->m_value,0,8 );
	init_bit_field ( &evtsel->m_umask,&evtsel->m_value,8,8 );
	init_bit_field ( &evtsel->m_usr,&evtsel->m_value,16,1 );
	init_bit_field ( &evtsel->m_os,&evtsel->m_value,17,1 );
	init_bit_field ( &evtsel->m_e,&evtsel->m_value,18,1 );
	init_bit_field ( &evtsel->m_pc,&evtsel->m_value,19,1 );
	init_bit_field ( &evtsel->m_int,&evtsel->m_value,20,1 );
	init_bit_field ( &evtsel->m_any,&evtsel->m_value,21,1 );
	init_bit_field ( &evtsel->m_en,&evtsel->m_value,22,1 );
	init_bit_field ( &evtsel->m_inv,&evtsel->m_value,23,1 );
	init_bit_field ( &evtsel->m_cmask,&evtsel->m_value,24,8 );
	init_bit_field ( &evtsel->m_nu2,&evtsel->m_value,32,32 );

	evtsel->m_value=0;
}


/* Enable available values */
enum {
	DISABLE_FIXED_EN=0,
	OS_FIXED_EN=1,
	USR_FIXED_EN=2,
	ENABLE_ALL_FIXED_EN=3
};

/* Fixed-count events */
enum {
	INSTR_RETIRED_ANY_FIXED=0,
	CPU_CLK_UNHALTED_CORE_FIXED=1,
	CPU_CLK_UNHALTED_REF_FIXED=2
};


/* MSR_PERF_FIXED_CTR_CTRL bit layout (Fixed-count events control register) */
typedef struct {
	bit_field_64 m_enable[NUM_FIXED_COUNTERS];	/* Enable Counter fields */
	bit_field_64 m_pmi[NUM_FIXED_COUNTERS];		/* Interrupt fields (PMIs) */
	uint64_t m_value; 				/* 64-bit store */
}
msr_perf_fixed_ctr_ctrl ;

/* Initialization function for MSR_PERF_FIXED_CTR_CTRL */
static inline void init_msr_perf_fixed_ctr_ctrl ( msr_perf_fixed_ctr_ctrl* ctrl )
{
	unsigned int offset=0;
	unsigned int i=0;

	/* Bit layout initialization for every fixed-count bitfield*/
	for(i=0; i<NUM_FIXED_COUNTERS; i++,offset+=4) {
		init_bit_field ( &ctrl->m_enable[i],&ctrl->m_value,offset,2 );
		init_bit_field ( &ctrl->m_pmi[i],&ctrl->m_value,offset+3,1 );
	}

	/* ZERO */
	ctrl->m_value=0;
}


/************ UMASK Available values ***************
 		(Table 18-7 to 18-10)
*/

/* Table 18-7*/
enum core_values {
	THIS_CORE=1,
	ALL_CORES=3
};

/* Table 18-8*/
enum agent_values {
	THIS_AGENT=0,
	ALL_AGENTS=1
};

/* Table 18-9*/
enum hw_prefetch_qualification_values {
	EXCLUDE_HW_PREFETCH=0,
	ONLY_HW_PREFETCH=1,
	ALL_INCLUSIVE_HW_PREFETCH=3
};
/* Table 18-10*/
enum mesi_bits {
	MESI_M=0x8,
	MESI_E=0x4,
	MESI_S=0x2,
	MESI_I=0x1,
	MESI_ALL=0xF
};



/* UMASK subfield bit layout ( PERFEVTSEL MSR field ) */
typedef struct {
	bit_field_8 m_core;		/* Per-core control field */
	bit_field_8 m_hw_prefetch;	/* Prefetch control field  */
	bit_field_8 m_mesi;		/* MESI-related control field  */
	uint8_t m_value; 			/* 8-bit store */
}
umask_subfield ;

static inline void init_umask_subfield ( umask_subfield* umask )
{
	/* Bit layout initialization */
	init_bit_field8 ( &umask->m_core,&umask->m_value,6,2 );
	init_bit_field8 ( &umask->m_hw_prefetch,&umask->m_value,4,2 );
	init_bit_field8 ( &umask->m_mesi,&umask->m_value,0,4 );
	umask->m_value=0;
}


typedef struct {
	/* Enumeration of bitfields*/
	bit_field_64 m_hardware_prefetcher_disable; 	/* Event Selection */
	_msr_t phys_msr;				/* 64-bit store */
}
ia32_misc_enable_msr;


typedef struct {
	/* Enumeration of bitfields*/
	bit_field_64 m_duty_cycle;
	bit_field_64 m_enable;
	_msr_t phys_msr;			/* 64-bit store */
}
ia32_clock_modulation_msr;




/* Initialization function for MSR_PERF_FIXED_CTR_CTRL */
static inline void init_ia32_clock_modulation_msr (ia32_clock_modulation_msr* clkmsr )
{

	/* Init MSR str */
	clkmsr->phys_msr.address=IA32_CLOCK_MODULATION_ADDR;
	clkmsr->phys_msr.reset_value=0;
	clkmsr->phys_msr.new_value=0;

	/* Init register manager (Point to new value) */
	init_bit_field ( &clkmsr->m_duty_cycle,&clkmsr->phys_msr.new_value,1,3 );
	init_bit_field ( &clkmsr->m_enable,&clkmsr->phys_msr.new_value,4,1 );


	/*
	- Read IA32_MISC_ENABLE
	 readMSR(&clkmsr->phys_msr);
	- Set a given value in one of the bits
	 set_bit_field(&clkmsr->m_turbo_mode_disable,val);
	 - Copy value in reset value -
	 clkmsr->phys_msr.reset_value=clkmsr->phys_msr.phys_msr.new_value;
	 - Make changes in the actual MSR
	 writeMSR(&clkmsr->phys_msr);
	 - Read the MSR Again to check it
	 readMSR(&clkmsr->phys_msr);
	*/
}


#define IA32_CLOCK_MODULATION_LEVELS		7


#endif /*PMCBITLAYOUT_H_*/
