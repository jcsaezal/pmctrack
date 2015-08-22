/*
 *  include/pmc/data_str/mc_allocator.h
 *
 * 	Stack-based simple memory allocator
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_MC_ALLOCATOR_H
#define PMC_MC_ALLOCATOR_H

#ifndef NULL
#define NULL 0
#endif


/* Representation of a free block */
typedef struct {
	unsigned int start_idx;		/* Starting Position */
	unsigned int size; 			/* Block Size */

} mc_freeblock;

/* Check extra conditions */
#ifndef MC_STACKS_SAFE_OPERATIONS
#define MC_STACKS_SAFE_OPERATIONS
#endif

/* Freeblock stacks (free space management) */
typedef struct {
	mc_freeblock* freeblocks;	/* Pointer to free blocks array */
	unsigned int  stack_size;  	/* Number of elements stored into the stack */
	unsigned int max_stack_size;	/* Max number of elements allowed (reserved space) */
} mc_freeblock_stack;


/* Returns a non-zero value when the stack is empty */
static inline void init_mc_freeblock_stack(mc_freeblock_stack* st,
        mc_freeblock* _freeblocks, unsigned int _max_stack_size)
{
	st->freeblocks=_freeblocks;
	st->max_stack_size=_max_stack_size;
	st->stack_size=0;
}


/* Returns a non-zero value when the stack is empty */
static inline int empty_mc_freeblock_stack(mc_freeblock_stack* st)
{
	return (st->stack_size==0);
}

/* Returns a non-zero value when the stack is full */
static inline int full_mc_freeblock_stack(mc_freeblock_stack* st)
{
	return (st->stack_size==st->max_stack_size);
}


/* Returns the size of the stack */
static inline unsigned int size_mc_freeblock_stack(mc_freeblock_stack* st)
{
	return st->stack_size;
}



/* Delete the first element in the stack */
static inline int  pop_mc_freeblock_stack(mc_freeblock_stack* st)
{
#ifdef MC_STACKS_SAFE_OPERATIONS
	if (st->stack_size!=0) {
		st->stack_size--;
		return 1;

	} else {
		return 0;
	}
#else
	st->stack_size--;
	return 1;
#endif

}

/* Insert at the top */
static inline int push_mc_freeblock_stack(mc_freeblock_stack* st, const mc_freeblock* item)
{
#ifdef MC_STACKS_SAFE_OPERATIONS
	if (st->stack_size < st->max_stack_size) {
		st->freeblocks[st->stack_size++]=(*item);
		return 1;
	} else
		return 0;
#else
	st->freeblocks[st->stack_size++]=(*item);
	return 1;
#endif

}

static inline mc_freeblock* top_mc_freeblock_stack(mc_freeblock_stack* st)
{
#ifdef MC_STACKS_SAFE_OPERATIONS
	if (st->stack_size!=0) {
		return &(st->freeblocks[st->stack_size-1]);
	} else {
		return NULL;
	}
#else
	return &(st->freeblocks[st->stack_size-1]);
#endif

}


/******************************* ALLOCATOR IMPLEMENTATION ********************************/


/* Allocator */
typedef struct {
	mc_freeblock_stack stack;	/* Stores the block */
	unsigned int free_count; 	/* Available blocks count*/
} mc_allocator;


static inline void init_mc_allocator(mc_allocator* allocator,
                                     mc_freeblock* blocks,
                                     unsigned int available_blocks )
{
	mc_freeblock fb;
	init_mc_freeblock_stack(&allocator->stack,blocks,available_blocks);

	/*At the begining, all blocks remain free*/
	allocator->free_count=available_blocks;

	fb.start_idx=0;
	fb.size=available_blocks;
	push_mc_freeblock_stack(&allocator->stack,&fb);
}


static inline int mc_malloc(mc_allocator* allocator) /* Just one element */
{
	unsigned int free_idx=0;
	if (allocator->free_count > 0) {
		/* We obtain the first free block */
		mc_freeblock *fb=top_mc_freeblock_stack(&allocator->stack);

		/*We operate with its value*/
		free_idx=fb->start_idx;
		fb->start_idx++;
		fb->size--;

		/* One element */
		allocator->free_count--;

		/* If the block gets empty
				we remove it from the stack */
		if (fb->size==0) {
			pop_mc_freeblock_stack(&allocator->stack);
		}

		return free_idx;
	} else {
		return -1;
	}

}

static inline int mc_free(mc_allocator* allocator, unsigned int blockidx)  /* Just one element */
{
	/* We obtain the first free block */
	mc_freeblock fb;
	fb.start_idx=blockidx;
	fb.size=1;

	/* Now it will go back to the stack*/
	push_mc_freeblock_stack(&allocator->stack,&fb);
	allocator->free_count++;
	return 0;
}





#endif
