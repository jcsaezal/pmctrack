/*
 *  include/pmc/arm/pmc_bit_layout.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef ARM_PMC_BIT_LAYOUT_H
#define ARM_PMC_BIT_LAYOUT_H


#include <pmc/data_str/bit_field.h>
#include <pmc/arm/pmu_asm.h>


#define clear_bit_layout(bl) (bl).m_value=0

/* PMXEVTYPER bit layout */
typedef struct {
	/* Enumeration of bitfields*/
	bit_field_32 m_evt_count; 	/* vent Selection*/
	bit_field_32 m_nu; 	/* NU */
	bit_field_32 m_nsh; 	/* NSH non-secure hypervisor */
	bit_field_32 m_nsu; 	/* NSU non-secure user */
	bit_field_32 m_nsk; 	/* NSK non-secure kernel */
	bit_field_32 m_u; 	/* User */
	bit_field_32 m_p; 	/* OS */
	uint32_t m_value; 	/* 32-bit store */
}
pmxevtyper_t;


static inline void init_pmxevtyper ( pmxevtyper_t* reg )
{
	/* Bit layout initialization */
	init_bit_field32 ( &reg->m_evt_count,&reg->m_value,0,8 );
	init_bit_field32 ( &reg->m_nu,&reg->m_value,8,19 );
	init_bit_field32 ( &reg->m_nsh,&reg->m_value,27,1 );
	init_bit_field32 ( &reg->m_nsu,&reg->m_value,28,1 );
	init_bit_field32 ( &reg->m_nsk,&reg->m_value,29,1 );
	init_bit_field32 ( &reg->m_u,&reg->m_value,30,1 );
	init_bit_field32 ( &reg->m_p,&reg->m_value,31,1 );
	reg->m_value=0;
}

/* PMXEVTYPER bit layout */
typedef struct {
	/* Enumeration of bitfields*/
	bit_field_32 m_e; 	/* (R/W) Enable/disable all counters including PMCCNTR */
	bit_field_32 m_p; 	/* (W) Reset all counters except the cycle counter */
	bit_field_32 m_c;	/* (W) Reset cycle counter */
	bit_field_32 m_d;	/* (R/W)  Clock divider for cycle counter only --> Use 0 here (every clock cycle) , if overflow issue -> use 1 (every 64 cycles) */
	bit_field_32 m_x;	/* (R/W) Export enable -> Use 0 here (meaningless) */
	bit_field_32 m_dp;	/* (R/W) Disable PMCCNTR when event counting is prohibited.  (Use 0 here -> enabled no matter what) */
	bit_field_32 m_nu;  	/* Reserved */
	bit_field_32 m_n;  	/* RO -> number of performance counters (from 0b00000 for no counters to 0b11111 for 31 counters.) */
	bit_field_32 m_idcode;  /* RO -> IDCODE */
	bit_field_32 m_imp;  	/* RO -> IMP CODE */
	uint32_t m_value; 	/* 32-bit store */
}
pmcr_t;

static inline void init_pmcr ( pmcr_t* reg )
{
	/* Bit layout initialization */
	/* Read register first off */
	reg->m_value=armv7_pmnc_read();
	init_bit_field32 ( &reg->m_e,&reg->m_value,0,1 );
	init_bit_field32 ( &reg->m_p,&reg->m_value,1,1 );
	init_bit_field32 ( &reg->m_c,&reg->m_value,2,1 );
	init_bit_field32 ( &reg->m_d,&reg->m_value,3,1 );
	init_bit_field32 ( &reg->m_x,&reg->m_value,4,1 );
	init_bit_field32 ( &reg->m_dp,&reg->m_value,5,1 );
	init_bit_field32 ( &reg->m_nu,&reg->m_value,6,5 );
	init_bit_field32 ( &reg->m_n,&reg->m_value,11,5 );
	init_bit_field32 ( &reg->m_idcode,&reg->m_value,16,8 );
	init_bit_field32 ( &reg->m_imp,&reg->m_value,24,8);
}


#endif /*PMCBITLAYOUT_H_*/
