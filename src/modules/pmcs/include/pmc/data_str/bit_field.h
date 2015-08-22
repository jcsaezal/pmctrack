/*
 *  include/pmc/data_str/bit_field.h
 *
 *	Abstract data types to deal with register bitwise operations
 *	in a more abstract, high-level manner 
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_BITFIELD_H
#define PMC_BITFIELD_H

#include <pmc/common/pmc_types.h>

typedef struct {
	uint64_t* data;			/* 64-bit store */
	size_t starting_bit;	/* First bit of the field */
	size_t n_bits;			/* Field length */
	uint64_t mask;			/* Enable Bitmask with 	[ starting_bit .. (starting_bit + n_bits -1) ] actived  */
} bit_field_64;

typedef struct {
	uint32_t* data;			/* 32-bit store */
	size_t starting_bit;	/* First bit of the field */
	size_t n_bits;			/* Field length */
	uint32_t mask;			/* Enable Bitmask with
							   [ starting_bit .. (starting_bit + n_bits -1) ]
					   		   actived
					 		 */
} bit_field_32;


typedef struct {
	uint8_t* data;			/* 8-bit store */
	size_t starting_bit;	/* First bit of the field */
	size_t n_bits;			/* Field length */
	uint8_t mask;			/* Enable Bitmask with
							   [ starting_bit .. (starting_bit + n_bits -1) ]
					   		   actived
					 		*/
} bit_field_8;


#define _INIT_BIT_FIELD {\
                size_t i=0;Ê\
                unsigned int offset=0; \
                (pbitf)->data=(data); \
                (pbitf)->starting_bit=(starting_bit); \
                (pbitf)->n_bits=(n_bits);       \
                (pbitf)->mask=0;\
                for (i=0; i<(pbitf)->n_bits; i++) {	\
                        (pbitf)->mask|=0x1ULL;	\
                                if (i<(pbitf)->n_bits-1) \
                                                (pbitf)->mask <<= 1; \
                }\
                if (pbitf->starting_bit>20) { \
                        offset=pbitf->starting_bit-20; \
                        (pbitf)->mask <<= 20; \
                        (pbitf)->mask <<= offset;\
                }else { (pbitf)->mask <<= (pbitf)->starting_bit;} \
                }

static inline void init_bit_field(bit_field_64* pbitf,uint64_t* data,size_t starting_bit,size_t n_bits)
{
	size_t i=0;
	unsigned int offset=0;
	(pbitf)->data=(data);
	(pbitf)->starting_bit=(starting_bit);
	(pbitf)->n_bits=(n_bits);
	(pbitf)->mask=0;
	for (i=0; i<(pbitf)->n_bits; i++) {
		(pbitf)->mask|=0x1ULL;
		if (i<(pbitf)->n_bits-1)
			(pbitf)->mask <<= 1;
	}
	if (pbitf->starting_bit>20) {
		offset=pbitf->starting_bit-20;
		(pbitf)->mask <<= 20;
		(pbitf)->mask <<= offset;
	} else {
		(pbitf)->mask <<= (pbitf)->starting_bit;
	}
}

static inline void init_bit_field32(bit_field_32* pbitf,uint_t* data,size_t starting_bit,size_t n_bits)
{
	size_t i=0;
	unsigned int offset=0;
	(pbitf)->data=(data);
	(pbitf)->starting_bit=(starting_bit);
	(pbitf)->n_bits=(n_bits);
	(pbitf)->mask=0;
	for (i=0; i<(pbitf)->n_bits; i++) {
		(pbitf)->mask|=0x1UL;
		if (i<(pbitf)->n_bits-1)
			(pbitf)->mask <<= 1;
	}
	if (pbitf->starting_bit>20) {
		offset=pbitf->starting_bit-20;
		(pbitf)->mask <<= 20;
		(pbitf)->mask <<= offset;
	} else {
		(pbitf)->mask <<= (pbitf)->starting_bit;
	}
}

static inline void init_bit_field8(bit_field_8* pbitf,uint8_t* data,size_t starting_bit,size_t n_bits)
{
	size_t i=0;
	(pbitf)->data=(data);
	(pbitf)->starting_bit=(starting_bit);
	(pbitf)->n_bits=(n_bits);
	(pbitf)->mask=0;

	for (i=0; i<(pbitf)->n_bits; i++) {
		(pbitf)->mask|=0x1;
		if (i<(pbitf)->n_bits-1)
			(pbitf)->mask <<= 1;
	}

	(pbitf)->mask <<= (pbitf)->starting_bit;
}


#define clear_bit_field(pbitf)	(*(pbitf)->data) &= ~((pbitf)->mask)


static inline void set_bit_field(bit_field_64* pbitf, uint64_t value)
{
	unsigned int offset=0;
	uint64_t val_mask=0x0000000000000000;


	/* 32-BIT COMPLIANT */

	if (pbitf->starting_bit>20) {
		offset=pbitf->starting_bit-20;
		val_mask=(value << 20);
		val_mask=(val_mask << offset);

	} else {
		val_mask = (value << pbitf->starting_bit);
	}

	/* Create masks in two steps*/

	clear_bit_field(pbitf);

	(*(pbitf)->data)|=(pbitf)->mask & (val_mask);

}

static inline uint64_t get_bit_field(bit_field_64* pbitf)
{
	unsigned int offset=0;
	uint64_t val_ret=(*(pbitf->data)) & pbitf->mask;


	/* 32-BIT COMPLIANT */

	if (pbitf->starting_bit>20) {
		offset=pbitf->starting_bit-20;
		val_ret>>=20;
		val_ret>>=offset;

	} else {
		val_ret>>=pbitf->starting_bit;
	}

	return val_ret;
}


static inline void set_bit_field32(bit_field_32* pbitf, uint_t value)
{
	unsigned int offset=0;
	uint64_t val_mask=0x00000000;


	/* 32-BIT COMPLIANT */

	if (pbitf->starting_bit>20) {
		offset=pbitf->starting_bit-20;
		val_mask=(value << 20);
		val_mask=(val_mask << offset);

	} else {
		val_mask = (value << pbitf->starting_bit);
	}

	/* Create masks in two steps*/

	clear_bit_field(pbitf);

	(*(pbitf)->data)|=(pbitf)->mask & (val_mask);

}



static inline uint_t get_bit_field32(bit_field_32* pbitf)
{
	unsigned int offset=0;
	uint_t val_ret=(*(pbitf->data)) & pbitf->mask;


	/* 32-BIT COMPLIANT */

	if (pbitf->starting_bit>20) {
		offset=pbitf->starting_bit-20;
		val_ret>>=20;
		val_ret>>=offset;

	} else {
		val_ret>>=pbitf->starting_bit;
	}

	return val_ret;
}
#endif

