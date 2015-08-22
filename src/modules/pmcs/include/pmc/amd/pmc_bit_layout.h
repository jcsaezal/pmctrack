/*
 *  include/pmc/amd/pmc_bit_layout.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef AMD_PMC_BIT_LAYOUT_H
#define AMD_PMC_BIT_LAYOUT_H


#include <pmc/data_str/bit_field.h>
#include <pmc/amd/pmc_const.h>


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
	bit_field_64 m_nu1; 	/* Reserved */
	bit_field_64 m_en;  	/* Enable counters */
	bit_field_64 m_inv; 	/* Invert flag */
	bit_field_64 m_cmask; 	/* CMASK */
	bit_field_64 m_evtsel2; /* Evtsel extension */
	bit_field_64 m_nu2; 	/* Reserved */
	bit_field_64 m_go; 	/* Host only */
	bit_field_64 m_ho; 	/* Guest only */
	bit_field_64 m_nu3; 	/* Reserved */
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
	init_bit_field ( &evtsel->m_nu1,&evtsel->m_value,21,1 );
	init_bit_field ( &evtsel->m_en,&evtsel->m_value,22,1 );
	init_bit_field ( &evtsel->m_inv,&evtsel->m_value,23,1 );
	init_bit_field ( &evtsel->m_cmask,&evtsel->m_value,24,8 );
	init_bit_field ( &evtsel->m_evtsel2,&evtsel->m_value,32,4);
	init_bit_field ( &evtsel->m_nu2,&evtsel->m_value,36,4);
	init_bit_field ( &evtsel->m_go,&evtsel->m_value,40,1);
	init_bit_field ( &evtsel->m_ho,&evtsel->m_value,41,1);
	init_bit_field ( &evtsel->m_nu3,&evtsel->m_value,42,22);

	evtsel->m_value=0;
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






#endif /*PMCBITLAYOUT_H_*/
