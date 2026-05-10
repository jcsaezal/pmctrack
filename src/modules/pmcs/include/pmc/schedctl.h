#ifndef PMC_SCHEDCTL_H
#define PMC_SCHEDCTL_H
#include <linux/types.h>
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

typedef struct {
	volatile unsigned char sc_coretype;
	volatile int sc_prio; /* To denote priority among threads */
	/* Legacy fields */
	volatile unsigned char 	sc_spinning;
	volatile unsigned int sc_nfc;
	volatile unsigned int sc_sf;
	volatile unsigned int sc_num_threads;
	volatile unsigned char sc_malleable;
} schedctl_t;


#endif