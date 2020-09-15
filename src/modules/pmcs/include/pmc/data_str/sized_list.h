#ifndef SIZED_LIST_H
#define SIZED_LIST_H
#ifdef __KERNEL__
#include <linux/list.h>
#else
#include <string.h>
#include <stdlib.h>
#include "list.h"
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

typedef struct sized_list {
	struct list_head list;
	size_t size;
	size_t node_offset;
} sized_list_t;


static inline size_t sized_list_length ( sized_list_t* slist)
{
	return slist->size;
};

/* Operations on slist_t */
static inline void init_sized_list (sized_list_t* slist, size_t node_offset)
{
	INIT_LIST_HEAD(&slist->list);
	slist->node_offset=node_offset;
	slist->size=0;
}

static inline void  insert_sized_list_tail ( sized_list_t* slist, void* elem)
{
	struct list_head* link=((struct list_head*)(((char*)elem) +  slist->node_offset));
	list_add_tail(link,&slist->list);
	slist->size++;
}


static inline void  insert_sized_list_head ( sized_list_t* slist, void* elem)
{
	struct list_head* link=((struct list_head*)(((char*)elem) +  slist->node_offset));
	list_add(link,&slist->list);
	slist->size++;
}


static inline void remove_sized_list ( sized_list_t* slist, void* elem)
{
	struct list_head* link=((struct list_head*)(((char*)elem) +  slist->node_offset));
	list_del(link);
	slist->size--;
}

static inline void* head_sized_list ( sized_list_t* slist)
{
	struct list_head *first=NULL;
	void* first_item=NULL;

	if (!list_empty(&slist->list)) {
		first = slist->list.next;
		first_item= ((char*)first) - slist->node_offset;
	}

	return first_item;
}

static inline void* tail_sized_list  ( sized_list_t* slist)
{
	struct list_head *last=NULL;
	void* last_item=NULL;

	if (!list_empty(&slist->list)) {
		last = slist->list.prev;
		last_item= ((char*)last) - slist->node_offset;
	}

	return last_item;
}

static inline void* next_sized_list ( sized_list_t* slist, void* elem)
{
	struct list_head *cur=NULL;

	if (!elem)
		return NULL;

	cur=((struct list_head*)(((char*)elem) +  slist->node_offset));

	/* Check if we reached the end of the list */
	if (cur->next==&slist->list)
		return NULL;

	/* Otherwise return item */
	return ((char*)cur->next) - slist->node_offset;

}

static inline void* prev_sized_list ( sized_list_t* slist, void* elem)
{
	struct list_head *cur=NULL;

	if (!elem)
		return NULL;

	cur=((struct list_head*)(((char*)elem) +  slist->node_offset));

	/* Check if we reached the end of the list */
	if (cur->prev==&slist->list)
		return NULL;

	/* Otherwise return item */
	return ((char*)cur->prev) - slist->node_offset;

}


static inline void insert_after_sized_list(sized_list_t* slist, void *object, void *nobject)
{
	struct list_head* prev_node=((struct list_head*)(((char*)object) +  slist->node_offset));
	struct list_head* new_node=((struct list_head*)(((char*)nobject) +  slist->node_offset));

	if (!object) {
		insert_sized_list_head(slist,nobject);
	} else {
		list_add(new_node,prev_node);
		slist->size++;
	}
}
static inline  void insert_before_sized_list(sized_list_t* slist, void *object, void *nobject)
{
	struct list_head* prev_node=((struct list_head*)(((char*)object) +  slist->node_offset));
	struct list_head* new_node=((struct list_head*)(((char*)nobject) +  slist->node_offset));

	if (!object) {
		insert_sized_list_tail(slist,nobject);
	} else {
		list_add_tail(new_node,prev_node);
		slist->size++;
	}
}

static inline int is_empty_sized_list (sized_list_t* slist)
{
	return slist->size==0;
}

#define CONFIG_LIST_SORT
#ifdef CONFIG_LIST_SORT

static inline void sorted_insert_sized_list(sized_list_t* slist, void* object, int ascending, int (*compare)(void*,void*))
{
	void *cur=NULL;

	cur=head_sized_list(slist);
	/* Search */
	if (ascending) {
		// Find
		while(cur!=NULL && compare(cur,object)<=0) {
			cur=next_sized_list(slist,cur);
		}
	} else {
		// Find
		while(cur!=NULL && compare(cur,object)>=0) {
			cur=next_sized_list(slist,cur);
		}
	}

	insert_before_sized_list(slist,cur,object);
}


static inline void sorted_insert_front_sized_list(sized_list_t* slist, void* object, int ascending, int (*compare)(void*,void*))
{
	void *cur=NULL;

	cur=head_sized_list(slist);
	/* Search */
	if (ascending) {
		// Find
		while(cur!=NULL && compare(cur,object)<0) {
			cur=next_sized_list(slist,cur);
		}
	} else {
		// Find
		while(cur!=NULL && compare(cur,object)>0) {
			cur=next_sized_list(slist,cur);
		}
	}

	insert_before_sized_list(slist,cur,object);
}

static inline void sort_sized_list(sized_list_t* slist, int ascending, int (*compare)(void*,void*))
{
	void *cur=NULL,*selected_node=NULL,*prev_selected=NULL;
	int i=0;

	/* Check if the list is already trivially sorted */
	if (slist->size<=1)
		return;

	cur=head_sized_list(slist);

	/* Insertion sort */
	for (i=0; i<slist->size-1 && cur!=NULL; i++) {

		/* Search */
		selected_node=cur;

		if (ascending) {
			// Search for min
			while(cur!=NULL) {
				if (compare(cur,selected_node)<0)
					selected_node=cur;
				cur=next_sized_list(slist,cur);
			}
		} else {
			// Search for max
			while(cur!=NULL) {
				if (compare(cur,selected_node)>0)
					selected_node=cur;
				cur=next_sized_list(slist,cur);
			}
		}

		remove_sized_list(slist,selected_node);
		insert_after_sized_list(slist,prev_selected,selected_node);
		prev_selected=selected_node;
		cur=next_sized_list(slist,selected_node);
	}
}
#endif

#endif

