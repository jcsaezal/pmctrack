/*
 *  include/pmc/data_str/cbuffer.h
 *
 * 	Byte ring buffer data structure
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef CBUFFER_H
#define CBUFFER_H


typedef struct {
	char* data;			/* raw byte vector */
	unsigned int head;		/* Index of the first element // head in [0 .. max_size-1] */
	unsigned int size;		/* Current Buffer size // size in [0 .. max_size] */
	unsigned int max_size;  	/* Buffer max capacity */
}
cbuffer_t;

typedef struct {
	cbuffer_t* cbuf;	/* Pointer to the cbuffer */
	int chunk_size;		/* Size of structures inserted in the cbuffer */
	int cur_item;		/* Item indicator */
	int cur_pos;		/* Position indicator (bytes) */
} iterator_cbuffer_t;

/* Operations supported by cbuffer_t */
/* Creates a new cbuffer (takes care of allocating memory) */
cbuffer_t* create_cbuffer_t (unsigned int max_size);

/* Release memory from circular buffer  */
void destroy_cbuffer_t ( cbuffer_t* cbuffer );

/* Returns the number of elements in the buffer */
int size_cbuffer_t ( cbuffer_t* cbuffer );

/* Returns the number of free gaps in the buffer */
int nr_gaps_cbuffer_t ( cbuffer_t* cbuffer );

/* Returns a non-zero value when buffer is full */
int is_full_cbuffer_t ( cbuffer_t* cbuffer );

/* Returns a non-zero value when buffer is empty */
int is_empty_cbuffer_t ( cbuffer_t* cbuffer );

/* Inserts an item at the end of the buffer */
void insert_cbuffer_t ( cbuffer_t* cbuffer, char new_item );

/* Inserts nr_items into the buffer */
void insert_items_cbuffer_t ( cbuffer_t* cbuffer, const void* items, int nr_items);

/* Removes the first element in the buffer and returns a copy of it */
char remove_cbuffer_t ( cbuffer_t* cbuffer);

/* Removes nr_items from the buffer and returns a copy of them */
void remove_items_cbuffer_t ( cbuffer_t* cbuffer, void* items, int nr_items);

/* Empty stuff from the buffer (whatever we've got inside) */
int remove_cbuffer_t_batch(cbuffer_t* cbuffer, void* items, int max_nr_items);

/* Removes all items in the buffer */
void clear_cbuffer_t (cbuffer_t* cbuffer);

/* Returns a pointer to the first element in the buffer */
char* head_cbuffer_t ( cbuffer_t* cbuffer );


/* Operations on iterator_cbuffer_t */
/* Build iterator */
iterator_cbuffer_t get_iterator_cbuffer_t(cbuffer_t* cbuf,int chunk_size);

/* Returns a pointer to the next item (NULL if the last one) */
void* iterator_next_cbuffer_t( iterator_cbuffer_t* it);

#endif
