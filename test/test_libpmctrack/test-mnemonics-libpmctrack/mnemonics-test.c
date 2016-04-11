/*
 * mnemonics-test.c
 *
 ******************************************************************************
 *
 * Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 ******************************************************************************
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pmctrack.h>

#define N 60000


int main(int argc, char *argv[])
{
	int i=0;
	int j=0;
	int k=0;
	int A[N],B[N],C[N];
	pmctrack_desc_t* desc;
#if defined(__arm__) || defined(__aarch64__)
	const char* strcfg[]= {"instr,cycles","0x8,llc_misses",NULL};
#else
	const char* strcfg[]= {"instr,cycles","0xc0,llc_misses",NULL};
#endif
	char* virtual_cfg=NULL; //"energy_pkg";

	/* Initialize the thread descriptor */
	if ((desc=pmctrack_init(100))==NULL)
		exit(1);

	/* Configure counters */
	if (pmctrack_config_counters_mnemonic(desc,strcfg,virtual_cfg,250,0))
		exit(1);

	/* Start counting */
	if (pmctrack_start_counters(desc))
		exit(1);

	/* Do something */
	for (k=0; k<1; k++)
		for (i=0; i<N; i++)
			for (j=i; j>=0; j--)
				C[j]=A[i]+B[j];

	/* Stop counting */
	if (pmctrack_stop_counters(desc))
		exit(1);

	printf("Values(%d,%d)\n", C[N/2],C[N-1]);

	/* Display information */
	pmctrack_print_counts(desc, stdout, 0);

	/* Free up memory */
	pmctrack_destroy(desc);

	exit(EXIT_SUCCESS);
}

