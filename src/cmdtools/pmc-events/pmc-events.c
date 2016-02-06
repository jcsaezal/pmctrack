/*
 * pmc-events.c
 *
 ******************************************************************************
 *
 * Copyright (c) 2015 Jorge Casas <jorcasas@ucm.es>
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
 *  2015-08-13  Modified by Juan Carlos Saez to allow listing virtual counters
 *				available
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <err.h>
#include <pmctrack_internal.h>

/*
 * Prints to stdout the list of virtual counters available,
 * together with additional comments.
 */
static inline void show_virtual_counters(int virtual_counters_only)
{
	if (pmct_get_nr_virtual_counters_supported()==0) {
		if (virtual_counters_only)
			printf("No virtual counters available\n");
	} else {
		printf("[Virtual counters]\n");
		pmct_print_virtual_counter_list();
	}
}

/*
 * Prints to stdout the list of hardware events and virtual counters
 * available for the current processor or other specified model.
 */
static void show_events(int idx_pmu, const char* processor_model, int verbose)
{
	pmu_info_t *pmu_info;
	int i=0;

	/* If the processor_model is forced, stick to PMU 0 then */
	if (processor_model)
		idx_pmu=0;

	if (idx_pmu<0) { /* List all */
		for (i=0; i<pmct_get_nr_pmus_model(processor_model); i++) {
			pmu_info = pmct_get_pmu_info(i,processor_model);
			printf("[PMU %i]\n", i);
			pmct_print_event_list(pmu_info,verbose);
		}
		/* List virtual counters as well */
		show_virtual_counters(0);
	} else {
		pmu_info = pmct_get_pmu_info(idx_pmu,processor_model);

		if (!pmu_info) {
			if (idx_pmu>= pmct_get_nr_pmus_model(processor_model))
				errx(1,"No such pmu: %d",idx_pmu);
			else
				errx(2,"Couldn't retrieve event listing");
		}

		printf("[PMU %i]\n", idx_pmu);
		pmct_print_event_list(pmu_info,verbose);
	}
}

/*
 * Prints to stdout the PMU's information for the current processor or other
 * specified.
 */
static void show_pmu_info(int idx_pmu, const char* processor_model, int verbose)
{
	pmu_info_t *pmu_info;
	int i=0;

	/* If the processor_model is forced, stick to PMU 0 then */
	if (processor_model)
		idx_pmu=0;

	if (idx_pmu<0) { /* List all */

		for (i=0; i<pmct_get_nr_pmus_model(processor_model); i++) {
			pmu_info = pmct_get_pmu_info(i,processor_model);
			printf("[PMU %i]\n", i);
			pmct_print_pmu_info(pmu_info,verbose);
		}
	} else {
		pmu_info = pmct_get_pmu_info(idx_pmu,processor_model);

		if (!pmu_info) {
			if (idx_pmu>= pmct_get_nr_pmus_model(processor_model))
				errx(1,"No such pmu: %d",idx_pmu);
			else
				errx(2,"Couldn't retrieve information on such pmu");
		}

		printf("[PMU %i]\n", idx_pmu);
		pmct_print_pmu_info(pmu_info,verbose);
	}
}

/* Prints to stdout the arguments accepted by the command */
static void usage(int verbose)
{
	printf("Usage: pmc-events [ -h | -v | -I | -L | -V | -n <pmun> | -e <event_string> | -m <processor_model> ]\n");

	if(verbose) {
		printf ("\n\t-h\n\t\tDisplays this information about the arguments available");
		printf ("\n\t-v\n\t\tShows extra information for some other options");
		printf ("\n\t-I\n\t\tDisplays information about the PMU of the processor model");
		printf ("\n\t-L\n\t\tList name of the hardware events and virtual counters available");
		printf ("\n\t-V\n\t\tList name of the virtual counters available");
		printf ("\n\t-n\t<pmun>\n\t\tEstablishes a number of PMU to use");
		printf ("\n\t-m\t<processor_model>\n\t\tSets another processor model");
		printf ("\n\t-e\t<event_string>\n\t\tTranslate string of mnemonics to raw kernel strings\n");
	}
}

/* MAIN */
int main(int argc, char* argv[])
{
	char optc;
	int verbose = 0, parse_string = 0, show_info = 0, list_events = 0, list_virtcount=0, no_opts = 1;
	int nr_pmu = -1;
	char *raw_cfgs[MAX_COUNTER_CONFIGS], *strcfg;
	unsigned int nr_exps, pmc_mask;
	counter_mapping_t *mapping;
	char* processor_model=NULL;
	int coretype=0;

	if (argc==1) {
		usage(1);
		exit(0);
	}

	while ((optc = getopt(argc, argv, "+he:LIn:vm:V")) != (char)-1) {
		no_opts = 0;
		switch (optc) {
		case 'v':
			verbose = 1;
			break;
		case 'h':
			usage(1);
			exit(0);
			break;
		case 'e':
			strcfg = optarg;
			parse_string = 1;
			break;
		case 'I':
			show_info = 1;
			break;
		case 'L':
			list_events = 1;
			break;
		case 'n':
			nr_pmu = atoi(optarg);
			break;
		case 'm':
			processor_model=malloc(strlen(optarg)+1);
			strcpy(processor_model,optarg);
			break;
		case 'V':
			list_virtcount =1;
			break;
		default:
			fprintf(stderr, "Wrong option: %c\n", optc);
			usage(0);
			exit(1);
		}
	}

	if(no_opts) {
		strcfg = argv[1];
		parse_string = 1;
	}

	if(show_info) {
		show_pmu_info(nr_pmu,processor_model,verbose);
	}
	if(list_events) {
		show_events(nr_pmu,processor_model,verbose);
	} else if (list_virtcount) {
		if (processor_model)
			printf("No virtual counters available\n");
		else
			show_virtual_counters(1);
	}

	if(parse_string) {
		mapping = (counter_mapping_t *)malloc(MAX_PERFORMANCE_COUNTERS*sizeof(counter_mapping_t));

		if (nr_pmu!=-1)
			coretype=nr_pmu;

		if(pmct_parse_counter_string(strcfg, coretype, processor_model, raw_cfgs, &nr_exps, &pmc_mask, mapping) == 0) {
			int i;
			for(i = 0; i < nr_exps; i++) {
				printf("%s\n", raw_cfgs[i]);
				free(raw_cfgs[i]);
			}
		} else {
			printf("Error processing string.\n");
		}
		free(mapping);
	}
	exit(0);
}
