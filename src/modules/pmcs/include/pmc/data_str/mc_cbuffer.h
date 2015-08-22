/*
 *  include/pmc/data_str/mc_cbuffer.h
 *
 * 	Integer ring buffer data structure 
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_MC_CBUFFER_H
#define PMC_MC_CBUFFER_H
#define MC_MAX_CBUFFER_SIZE 20


/***************************************************************************************************************/
/**************** Circular buffer which stores threads' last monitoring history
 ********************************/

typedef struct {
	unsigned long data[MC_MAX_CBUFFER_SIZE];
	unsigned int head;	/* Index of the first element // head e [0 .. max_size-1] */
	unsigned int size;	/* Current Buffer size // size e [0 .. max_size] */
	unsigned int max_size;  /* Buffer max cappacity // 0 < max_size <= MC_MAX_CBUFFER_SIZE */
}
mc_cbuffer;


/* Circular buffer initilization */
static inline void init_mc_cbuffer ( mc_cbuffer* cbuffer,unsigned int max_size )
{
	cbuffer->size=0;
	cbuffer->head=0;
	if ( max_size<=MC_MAX_CBUFFER_SIZE ) {
		cbuffer->max_size=max_size;
	} else {
		cbuffer->max_size=MC_MAX_CBUFFER_SIZE;
	}
}

/* Return a non-zero value when buffer is full */
static inline int is_full_mc_cbuffer ( mc_cbuffer* cbuffer )
{
	return ( cbuffer->size == cbuffer->max_size ) ;
}

/* Return a non-zero value when buffer is empty */
static inline int is_empty_mc_cbuffer ( mc_cbuffer* cbuffer )
{
	return ( cbuffer->size == 0 ) ;
}


/* Clear the contents of the buffer */
static inline void clear_mc_cbuffer ( mc_cbuffer* cbuffer )
{
	cbuffer->size=0;
	cbuffer->head=0;
}


/* Insert an item at the end of the buffer */
static inline void push_mc_cbuffer ( mc_cbuffer* cbuffer,unsigned long new_value )
{
	unsigned int pos=0;

	/* The buffer is full */
	if ( cbuffer->size == cbuffer->max_size ) {
		/* Overwriting head position */
		cbuffer->data[cbuffer->head]=new_value;
		/* Now head position must be the next one*/
		if ( cbuffer->size !=0 )
			cbuffer->head= ( cbuffer->head+1 ) % cbuffer->max_size;
		/* Size remains constant*/
	}

	else {
		if ( cbuffer->max_size!=0 )
			pos= ( cbuffer->head+cbuffer->size ) % cbuffer->max_size;
		cbuffer->data[pos]=new_value;
		cbuffer->size++;
	}

}

static inline void pop_mc_cbuffer ( mc_cbuffer* cbuffer)
{
	if ( cbuffer->size !=0 ) {
		cbuffer->head= ( cbuffer->head+1 ) % cbuffer->max_size;
		cbuffer->size--;
	}
}

static inline unsigned long* head_mc_cbuffer ( mc_cbuffer* cbuffer )
{
	if ( cbuffer->size !=0 )
		return &cbuffer->data[cbuffer->head];
	else {
		return NULL;
	}
}


#endif // PMC_MC_CBUFFER_H
