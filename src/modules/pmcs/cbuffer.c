/*
 *  cbuffer.c
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/data_str/cbuffer.h>
#ifdef __KERNEL__
#include <linux/vmalloc.h> /* vmalloc()/vfree()*/
#include <linux/slab.h>
#include <asm/string.h> /* memcpy() */
#else
#include <stdlib.h>
#include <string.h>
#endif

#ifndef NULL
#define NULL 0
#endif

#ifdef __KERNEL__
#define buf_mem_alloc(bytes) kmalloc(bytes,GFP_KERNEL)
#define buf_mem_free(bytes) kfree(bytes)
#else
#define buf_mem_alloc(bytes) malloc(bytes)
#define buf_mem_free(bytes) free(bytes)
#endif



/* Create cbuffer */
cbuffer_t* create_cbuffer_t (unsigned int max_size)
{
	cbuffer_t *cbuffer= (cbuffer_t *)buf_mem_alloc(sizeof(cbuffer_t));

	if (cbuffer == NULL) {
		return NULL;
	}

	cbuffer->size=0;
	cbuffer->head=0;
	cbuffer->max_size=max_size;

	/* Stores bytes */
	cbuffer->data=buf_mem_alloc(max_size);

	if ( cbuffer->data == NULL) {
		buf_mem_free(cbuffer->data);
		return NULL;
	}
	return cbuffer;
}

/* Release memory from circular buffer  */
void destroy_cbuffer_t ( cbuffer_t* cbuffer )
{
	cbuffer->size=0;
	cbuffer->head=0;
	cbuffer->max_size=0;

	buf_mem_free(cbuffer->data);
	buf_mem_free(cbuffer);
}

/* Returns the number of elements in the buffer */
int size_cbuffer_t ( cbuffer_t* cbuffer )
{
	return cbuffer->size ;
}

/* Returns the number of free gaps in the buffer */
int nr_gaps_cbuffer_t ( cbuffer_t* cbuffer )
{
	return cbuffer->max_size-cbuffer->size;
}

/* Return a non-zero value when buffer is full */
int is_full_cbuffer_t ( cbuffer_t* cbuffer )
{
	return ( cbuffer->size == cbuffer->max_size ) ;
}

/* Return a non-zero value when buffer is empty */
int is_empty_cbuffer_t ( cbuffer_t* cbuffer )
{
	return ( cbuffer->size == 0 ) ;
}


/* Inserts an item at the end of the buffer */
void insert_cbuffer_t ( cbuffer_t* cbuffer, char new_item )
{
	unsigned int pos=0;
	/* The buffer is full */
	if ( cbuffer->size == cbuffer->max_size ) {
		/* Overwriting head position */
		cbuffer->data[cbuffer->head]=new_item;
		/* Now head position must be the next one*/
		if ( cbuffer->size !=0 )
			cbuffer->head= ( cbuffer->head+1 ) % cbuffer->max_size;
		/* Size remains constant*/
	} else {
		if ( cbuffer->max_size!=0 )
			pos= ( cbuffer->head+cbuffer->size ) % cbuffer->max_size;
		cbuffer->data[pos]=new_item;
		cbuffer->size++;
	}
}

/* Inserts nr_items into the buffer */
void insert_items_cbuffer_t ( cbuffer_t* cbuffer, const void* vitems, int nr_items)
{
	char* items=(char*)vitems;
	int nr_items_left=nr_items;
	int items_copied;
	int nr_gaps=cbuffer->max_size-cbuffer->size;
	int whead=(cbuffer->head+cbuffer->size)%cbuffer->max_size;

	/* Restriction: nr_items can't be greater than the max buffer size) */
	if (nr_items>cbuffer->max_size)
		return;

	/* Check if we can't store all items at the end of the buffer */
	if (whead+nr_items_left > cbuffer->max_size) {
		items_copied=cbuffer->max_size-whead;
		memcpy(&cbuffer->data[whead],items,items_copied);
		nr_items_left-=items_copied;
		items+=items_copied; //Move the pointer forward
		whead=0;
	}

	/* If we still have to copy elements -> do it*/
	if (nr_items_left) {
		memcpy(&cbuffer->data[whead],items,nr_items_left);
		whead+=nr_items_left;
	}

	/* Update size and head */
	if (nr_gaps>=nr_items) {
		cbuffer->size+=nr_items;
	} else {
		cbuffer->size=cbuffer->max_size;
		/* head moves in the event we overwrite stuff */
		cbuffer->head=(cbuffer->head+(nr_items-nr_gaps))% cbuffer->max_size;
	}
}

/* Removes nr_items from the buffer and returns a copy of them */
void remove_items_cbuffer_t ( cbuffer_t* cbuffer, void* vitems, int nr_items)
{
	char* items=(char*)vitems;
	int nr_items_left=nr_items;
	int items_copied;

	/* Restriction: nr_items can't be greater than the buffer size (Ignore)) */
	if (nr_items>cbuffer->size)
		return;

	/* Check if we can't store all items at the end of the buffer */
	if (cbuffer->head+nr_items_left > cbuffer->max_size) {
		items_copied=cbuffer->max_size-cbuffer->head;
		memcpy(items,&cbuffer->data[cbuffer->head],items_copied);
		nr_items_left-=items_copied;
		items+=items_copied; //Move the pointer forward
		cbuffer->head=0;
	}


	/* If we still have to copy elements -> do it*/
	if (nr_items_left) {
		memcpy(items,&cbuffer->data[cbuffer->head],nr_items_left);
		cbuffer->head+=nr_items_left;
	}

	/* Update size */
	cbuffer->size-=nr_items;
}

int remove_cbuffer_t_batch(cbuffer_t* cbuffer, void* items, int max_nr_items)
{
	/* Check the maximum number of bytes we can actually retrieve */
	int nr_items=max_nr_items>cbuffer->size?cbuffer->size:max_nr_items;

	/* Remove items from the buffer!! */
	if (nr_items)
		remove_items_cbuffer_t (cbuffer, items, nr_items);

	return nr_items;
}


/* Remove first element in the buffer */
char remove_cbuffer_t ( cbuffer_t* cbuffer)
{
	char ret='\0';

	if ( cbuffer->size !=0 ) {
		ret=cbuffer->data[cbuffer->head];
		cbuffer->head= ( cbuffer->head+1 ) % cbuffer->max_size;
		cbuffer->size--;
	}

	return ret;
}

/* Removes all items in the buffer */
void clear_cbuffer_t (cbuffer_t* cbuffer)
{
	cbuffer->size = 0;
	cbuffer->head = 0;
}

/* Returns the first element in the buffer */
char* head_cbuffer_t ( cbuffer_t* cbuffer )
{
	if ( cbuffer->size !=0 )
		return &cbuffer->data[cbuffer->head];
	else {
		return NULL;
	}
}

/* Build iterator */
iterator_cbuffer_t get_iterator_cbuffer_t(cbuffer_t* cbuf,int chunk_size)
{
	iterator_cbuffer_t it;

	it.cbuf=cbuf;
	it.chunk_size=chunk_size;
	it.cur_item=0;
	it.cur_pos=cbuf->head;

	return it;
}

/* Returns a pointer to the next item (NULL if the last one) */
void* iterator_next_cbuffer_t( iterator_cbuffer_t* it)
{
	cbuffer_t* cbuf=it->cbuf;
	void* ptr;

	if (it->cur_item>=cbuf->size)
		return NULL;

	ptr=&cbuf->data[it->cur_pos];
	it->cur_item+=it->chunk_size;
	it->cur_pos+=it->chunk_size;

	if (it->cur_pos>=cbuf->max_size)
		it->cur_pos-=cbuf->max_size;

	return ptr;
}

