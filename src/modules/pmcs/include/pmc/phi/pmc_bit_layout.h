/*
 *  include/pmc/phi/pmc_bit_layout.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PHI_PMC_BIT_LAYOUT_H
#define PHI_PMC_BIT_LAYOUT_H

#include <pmc/data_str/bit_field.h>
#include <pmc/phi/pmc_const.h>


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
	init_bit_field ( &evtsel->m_nu1,&evtsel->m_value,21,1 );
	init_bit_field ( &evtsel->m_en,&evtsel->m_value,22,1 );
	init_bit_field ( &evtsel->m_inv,&evtsel->m_value,23,1 );
	init_bit_field ( &evtsel->m_cmask,&evtsel->m_value,24,8 );
	init_bit_field ( &evtsel->m_nu2,&evtsel->m_value,32,32 );

	evtsel->m_value=0;
}




#endif /*PHI_PMC_BIT_LAYOUT_H*/
