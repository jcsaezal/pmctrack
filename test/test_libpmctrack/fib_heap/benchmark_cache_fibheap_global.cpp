/*
 * benchmark_cache_fibheap_global.cpp
 *
 ******************************************************************************
 *
 * Copyright 2015 Abel Serrano Juste <abeserra@ucm.es>
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
 *  Created on: 19-ene-2015
 *      Author: Abel Serrano Juste
 */

#include <iostream>
#include <sys/time.h>	/* For gettimeofday(). Attention! linux only!! is not platform independent! */
#include <stdlib.h>     /* srand, rand */

#include "fib_heap.h" // Fibonacci's heap

extern "C"{
#include <pmctrack.h> // profiling using libpmctrack
}

// PMC Config here:
//   I want to count instructions nr and cache acesses/fails
#if defined(AMD)
/* string conf. of PMCs for AMD
 * 0xc0 -> Retired instructions
 * 0x4e0 + umask=0xf7 -> L3 Cache Accesses in any core
 * 0x4e1 + umask=0xf7 -> L3 Cache Misses in any core
 * */
const char* strcfg[] = {"pmc0=0xc0,pmc1=0x4e0,umask1=0xf7,pmc2=0x4e1,umask2=0xf7",NULL};

#else // Intel
/* string conf. of PMCs for Intel
 * 0xc0 -> Retired instructions
 * 0x2e umask=0x4f -> Last Level Cache Accesses
 * 0x2e umask=0x41 -> Last Level Cache Cache Misses
 * */
const char* strcfg[] = {"pmc0,pmc3=0x2e,umask3=0x4f,pmc4=0x2e,umask4=0x41",NULL};
#endif

char* virtcfg = NULL;

pmctrack_desc_t* desc;

#undef VERBOSE
#ifdef VERBOSE
#define V(x) do { x; } while (0)
#else
#define V(x)
#endif

using namespace fibh;
using namespace std;

#define MAX_SAMPLES 15
#define TIMEOUT 250

int main(int argc, char **argv)
{
  /* libpmctrack: init */
  if ((desc=pmctrack_init(MAX_SAMPLES))==NULL)
    exit(1);

  /* libpmctrack: configure counters */
  if (pmctrack_config_counters(&desc,strcfg,virtcfg,TIMEOUT))
    exit(1);

	const unsigned input_n = 1000000; //inserting 1000000 numbers
	const unsigned range = 10000; // in the range from -5000 to 5000

	/* Timer declarations */
	struct timeval start, end;
  long seconds, useconds;
  long unsigned int mtime;
  /* Timer init */
  gettimeofday(&start, NULL);
  /* initialize random seed with start time: */
	srand (start.tv_sec);

  /* libpmctrack: Start counting */
  if (pmctrack_start_counters(&desc))
      exit(1);

  /* Benchmark starts here */

	fib_heap fh;

	for (unsigned int i = 0; i<input_n; i++)
	{
		int randn = (rand() % range + 1) - range/2;
		fh.insert(randn);
	}

	for (unsigned int i = 0; i<input_n; ++i)
	{
		int aux_min = fh.deleteMin();

		V(
    cout << "Data after "<< i <<"th deletion:" << endl;
    cout << "Min extracted: " << aux_min << endl;
    cout << "Printing heap:" << endl;
		cout << fh << endl;
		cout << "**************************************" << endl);
	}
	/* Benchmark ends here */

  /* libpmctrack: Stop counting */
  if (pmctrack_stop_counters(&desc))
      exit(1);

	/* Timer: Calculating elapsed time */
	gettimeofday(&end, NULL);

	useconds = end.tv_usec - start.tv_usec;
  seconds  = end.tv_sec  - start.tv_sec;

  mtime = ((seconds) * 1000000 + useconds) + 0.5;

  cout << "Elapsed time: " << mtime << " microseconds" << endl;

  cout << "Profiling data extracted from PMCs every "<< TIMEOUT << "ms:" << endl;

  /* libpmctrack: Display information about the PMCs */
  pmctrack_print_counts(&desc, stdout, 0);

  /* libpmctrack: Free up memory */
  pmctrack_destroy(&desc);

	return 0;
}

