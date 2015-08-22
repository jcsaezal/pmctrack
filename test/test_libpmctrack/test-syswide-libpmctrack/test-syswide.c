/*
 * test-syswide.c
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


/* MAIN */
int main(int argc, char *argv[])
{
	int i=0;
	int j=0;
	int k=0;
	int A[N],B[N],C[N];
	pmctrack_desc_t* desc;
	const char* strcfg[]= {
#if defined(__arm__) || defined(__aarch64__)
		"pmc1=0x11,pmc2=0x08"
#elif defined(AMD)
		"pmc0=0xc0,pmc1=0x76"
#else
		"pmc0,pmc1,pmc2,pmc3=0x2e,umask3=0x4f,pmc4=0x2e,umask4=0x41"
#endif
		,NULL
	};

	/* Initialize the thread descriptor */
	if ((desc=pmctrack_init(100))==NULL)
		exit(1);

	/* Configure counters */
	if (pmctrack_config_counters(desc,strcfg,NULL,1000))
		exit(1);


	/* Start counting */
	if (pmctrack_start_counters_syswide(desc))
		exit(1);

	/* Do something */
	for (k=0; k<1; k++)
		for (i=0; i<N; i++)
			for (j=i; j>=0; j--)
				C[j]=A[i]+B[j];

	/* Stop counting */
	if (pmctrack_stop_counters_syswide(desc))
		exit(1);

	printf("Values(%d,%d)\n", C[N/2],C[N-1]);

	/* Display information */
	pmctrack_print_counts(desc, stdout, 0);

	/* Free up memory */
	pmctrack_destroy(desc);

	exit(EXIT_SUCCESS);
}

