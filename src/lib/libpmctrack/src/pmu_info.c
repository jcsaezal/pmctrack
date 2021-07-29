/*
 * pmu-info.c
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
 * 2015-08-07 Modified by Juan Carlos Saez to move the code of low-level routines
 *            defined previously in pmc-events.c (command-line tool).
 * 2015-08-12 Modified by Juan Carlos Saez to allow listing virtual counters
 *            and to include functions to translate mnemonic-based virtual
 *            counter configuration strings into the raw format.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <err.h>
#include "pmctrack_internal.h"

#define COMPLETE_EVENT_CFG_SIZE 100

typedef struct {
	int nr_counter;
	int nr_exp;
	char name[COMPLETE_EVENT_CFG_SIZE];
	char code[CODE_HW_EVENT_SIZE];
	pmc_property_t *properties[MAX_NR_PROPERTIES];
	unsigned int nr_properties;
} event_cfg_t;


/* Reduced set of global variables */
static pmu_info_t** pmu_info_vector_gbl=NULL;
static int nr_pmus_gbl=-1;
static virtual_counter_info_t* virtual_counter_info_gbl=NULL;

/*
 * Get PMU and virtual counters information from /proc/pmc/info and stores it in the data
 * structures passed by arguments.
 * Returns a value less than 0 if an error occurs, or 0 if successful.
 */
static int parse_pmc_info_file(pmu_info_t** pmu_info_vector, virtual_counter_info_t* vcount_info)
{
	char line[FILE_LINE_SIZE];
	char str_value[FILE_LINE_SIZE];
	unsigned int int_value;
	int i = 0;
	FILE *f = fopen("/proc/pmc/info", "r");
	if (f == NULL) {
		warnx("Can't open pmc info file.");
		return -1;
	}

	while(fgets(line, FILE_LINE_SIZE, f) && !sscanf(line, "nr_core_types=%u",&nr_pmus_gbl)) {}

	if(nr_pmus_gbl <= 0)
		return -2;

	for (i = 0; i < nr_pmus_gbl; i++)
		pmu_info_vector[i] = (pmu_info_t *)malloc(sizeof(pmu_info_t));

	i = 0;

	while(!feof(f) && fgets(line, FILE_LINE_SIZE, f)) {
		if(sscanf(line, "pmu_model=%s", str_value) == 1) {
			strncpy(pmu_info_vector[i]->model, str_value, NAME_MODEL_SIZE);
		} else if(sscanf(line, "nr_gp_pmcs=%u", &int_value) == 1) {
			pmu_info_vector[i]->nr_gp_pmcs = int_value;
		} else if(sscanf(line, "nr_ff_pmcs=%u", &int_value) == 1) {
			pmu_info_vector[i]->nr_fixed_pmcs = int_value;
		} else if (sscanf(line, "[PMU coretype%d]",&int_value) ==1 && int_value>0) {
			i++;
		} else if (sscanf(line, "nr_virtual_counters=%d",&int_value) ==1) {
			vcount_info->nr_virtual_counters=int_value;
		} else if (sscanf(line, "virt%d=%s", &int_value, str_value) == 2) {
			vcount_info->name[int_value]=malloc(strlen(str_value)+1);
			strcpy(vcount_info->name[int_value],str_value);
		}
	}
	fclose(f);
	return 0;
}

/*
 * Gets the csv file of the PMU info received by argument and fills this PMU info
 * with event and sub-events information of this csv file.
 * Returns a value less than 0 if an error occurs, or 0 if successful.
 */
static int parse_csv_file(pmu_info_t *pmu_info)
{
	char path_csv[PATH_CSV_SIZE];
	char line[FILE_LINE_SIZE];
	char *row, *evt_name, *subevt_name, *evt_code, *flags, *flag, *key, *value;
	int found, ind;
	FILE *f = NULL;

	/* Generate path to the corresponding CSV file */
	if(getenv("PMCTRACK_ROOT") != NULL) {
		strcpy(path_csv, getenv("PMCTRACK_ROOT"));
		strcat(path_csv, "/etc/events/");
	} else
		strcpy(path_csv, "/home/jorge/git/pmctrack/etc/events/");
	strcat(path_csv, pmu_info->model);
	strcat(path_csv, ".csv");

	if (!(f= fopen(path_csv, "r"))) {
		warnx("%s",path_csv);
		return 1;
	}

	pmu_info->nr_events = 0;
	fgets(line, FILE_LINE_SIZE, f); /* Dismiss the head of the csv file */
	while(!feof(f) && fgets(line, FILE_LINE_SIZE, f)) {
		row = line;

		/* Discard empty lines */
		if (strcmp(row,"\n")==0 || strcmp(row,"")==0)
			continue;

		evt_name = strsep(&row, ",");
		subevt_name = strsep(&row, ",");
		evt_code = strsep(&row, ",");
		/* Ignore description */
		strsep(&row, ",");
		flags = strsep(&row, ",");
		ind = found = 0;
		while(ind < pmu_info->nr_events && !found)
			if(strcmp(pmu_info->events[ind]->name, evt_name) == 0) {
				found = 1;
			} else {
				ind++;
			}

		if(!found) { /* Create a new event */
			if(ind >= MAX_NR_EVENTS) {
				warnx("Array events is full.");
				return -2;
			}
			hw_event_t *hw_evt = (hw_event_t *)malloc(sizeof(hw_event_t));
			hw_evt->pmcn = -1;
			strcpy(hw_evt->name, evt_name);
			strcpy(hw_evt->code, evt_code);
			hw_evt->nr_subevents = 0;
			pmu_info->events[ind] = hw_evt;
			(pmu_info->nr_events)++;
		}

		/* In any case, create a sub-event (default sub-event if it just created the event) */
		if(pmu_info->events[ind]->nr_subevents >= MAX_NR_SUBEVENTS) {
			warnx("Array subevents is full.");
			return -3;
		}

		hw_subevent_t *hw_subevt = (hw_subevent_t *)malloc(sizeof(hw_subevent_t));
		strcpy(hw_subevt->name, subevt_name);
		hw_subevt->nr_properties = 0;

		/* Remove linebreak */
		if (flags && flags[strlen(flags)-1]=='\n')
			flags[strlen(flags)-1]='\0';

		while((flag = strsep(&flags, ";")) != NULL) {
			key = strsep(&flag, "=");
			value = strsep(&flag, "=");
			if(strcmp(key, "pmc") == 0) {
				pmu_info->events[ind]->pmcn = atoi(value);
			} else if(strncmp(key, "-", 1) != 0 && strcmp(key, "type") != 0) {
				pmc_property_t *property = (pmc_property_t *)malloc(sizeof(pmc_property_t));
				strncpy(property->key, key, NAME_KEY_PMC_PROPERTY_SIZE);
				if(value)
					strncpy(property->value, value, VALUE_PMC_PROPERTY_SIZE);
				else
					property->value[0] = '\0';
				hw_subevt->properties[hw_subevt->nr_properties] = property;
				(hw_subevt->nr_properties)++;
			}

		}
		pmu_info->events[ind]->subevents[pmu_info->events[ind]->nr_subevents] = hw_subevt;
		(pmu_info->events[ind]->nr_subevents)++;

	}
	fclose(f);
	return 0;
}

// void free_pmu_info(pmu_info_t *pmu_info) {
// 	int i, j, x;
// 	for(i = 0; i < pmu_info->nr_events; i++){
// 		for(j = 0; j < pmu_info->events[i]->nr_subevents; j++){
// 			for(x = 0; x < pmu_info->events[i]->subevents[j]->nr_properties; x++)
// 				free(pmu_info->events[i]->subevents[j]->properties[x]);
// 			free(pmu_info->events[i]->subevents[j]);
// 		}
// 		free(pmu_info->events[i]);
// 	}
// 	free(pmu_info);
// }

/* Build a PMU info invented for the processor model passed by argument */
static int build_default_pmu_info(pmu_info_t** pmu_info_vector, const char* processor_model)
{
	pmu_info_t* pmu_info;

	nr_pmus_gbl=1; /* Assume symmetric system */
	pmu_info= pmu_info_vector[0] = (pmu_info_t *)malloc(sizeof(pmu_info_t));

	if (!pmu_info)
		return 1;

	strncpy(pmu_info->model, processor_model, NAME_MODEL_SIZE);
	pmu_info->nr_gp_pmcs = 4;
	pmu_info->nr_fixed_pmcs = 0;
	return 0;
}

/* Retrieve the information associated with a given PMU */
pmu_info_t* pmct_get_pmu_info(unsigned int nr_coretype, const char* processor_model)
{

	int i = 0;

	if (!pmu_info_vector_gbl) {

		pmu_info_vector_gbl=(pmu_info_t **)malloc(sizeof(pmu_info_t *)*MAX_CORE_TYPES);
		for (i = 0; i < MAX_CORE_TYPES; i++)
			pmu_info_vector_gbl[i]=NULL;

		/* Reserve memory for this as well */
		virtual_counter_info_gbl=malloc(sizeof(virtual_counter_info_t));

		if (processor_model) {
			if(build_default_pmu_info(pmu_info_vector_gbl,processor_model) != 0)
				goto free_up_on_error;
		} else {
			if(parse_pmc_info_file(pmu_info_vector_gbl,virtual_counter_info_gbl) != 0)
				goto free_up_on_error;
		}
		for (i = 0; i < nr_pmus_gbl; i++) {
			if(parse_csv_file(pmu_info_vector_gbl[i]) != 0)
				goto free_up_on_error;
		}
	}
	if (nr_coretype >= nr_pmus_gbl)
		return NULL;
	else
		return pmu_info_vector_gbl[nr_coretype];

free_up_on_error:

	if (pmu_info_vector_gbl) {
		for (i = 0; i < MAX_CORE_TYPES; i++)
			/* Warning: A more sophisticated free function shold be included
				for individual vector components */
			if (pmu_info_vector_gbl[i])
				free(pmu_info_vector_gbl[i]);
		free(pmu_info_vector_gbl);
		pmu_info_vector_gbl=NULL;
	}

	if (virtual_counter_info_gbl) {
		free(virtual_counter_info_gbl);
		virtual_counter_info_gbl=NULL;
	}

	return NULL;
}

/*
 * Get the number of PMUs associated with a given processor model.
 * If NULL is passed as a parameter, the function provides the PMU count
 * of the current machine.
 */
int pmct_get_nr_pmus_model(const char* processor_model)
{
	/* Force reading information from files if necessary */
	if (nr_pmus_gbl==-1)
		pmct_get_pmu_info(0,processor_model);

	return nr_pmus_gbl;
}

/* Get the number of PMUs detected in the current machine. */
int pmct_get_nr_pmus(void)
{
	return pmct_get_nr_pmus_model(NULL);
}

/*
 * Get the number of virtual counters available
 * (Note: this value depends on the current active
 *   monitoring module)
 */
int pmct_get_nr_virtual_counters_supported(void)
{
	/* Force reading information from files if necessary */
	if (!virtual_counter_info_gbl)
		pmct_get_pmu_info(0,NULL);

	if (virtual_counter_info_gbl)
		return virtual_counter_info_gbl->nr_virtual_counters;
	else return 0;
}

/*
 * Retrieve information on virtual counters
 * (Note: this value depends on the current active
 *   monitoring module)
 */
virtual_counter_info_t* pmct_get_virtual_counter_info(void)
{
	/* Force reading information from files if necessary */
	if (!virtual_counter_info_gbl)
		pmct_get_pmu_info(0,NULL);
	return virtual_counter_info_gbl;
}

/*
 * This function takes care of translating a mnemonic-based
 * PMC configuration string into the raw format.
 * Note that we may run out of physical counters to monitor
 * the requested events. In that case, extra experiments may be
 * allocated. As such, the function returns an array of
 * raw-formatted (feasible) configuration strings.
 */
int pmct_parse_counter_string(const char *strcfg, int nr_coretype,
                              const char* processor_model,
                              char *raw_cfgs[],
                              unsigned int *nr_experiments,
                              unsigned int *used_counter_mask,
                              counter_mapping_t *mapping)
{

	event_cfg_t *events_cfg[MAX_NR_EVENTS_CFG];
	unsigned int nr_events_cfg = 0;
	event_cfg_t *evt_cfg;
	pmc_property_t *property_evt;
	char row_cfg[ROW_CFG_SIZE];
	char *row_ptr, *evt_row, *field_evt, *key_field_evt, *value_field_evt;
	unsigned int nr_exps, actual_exp, highest_exp = 0;
	unsigned int pmc_mask = 0;
	int found, subfound, ind, subind, x;
	pmu_info_t *pmu_info = pmct_get_pmu_info(nr_coretype,processor_model);
	hw_subevent_t* cur_subevent;
	int max_core_types=pmct_get_nr_pmus_model(processor_model);
	int coretype=-1; /* In the event the coretype was forced in the high-level string */
	char* orig_evt_row=NULL;

	if(!pmu_info)
		return -1; /* Error getting PMU information */

	strncpy(row_cfg, strcfg, ROW_CFG_SIZE);
	row_ptr = row_cfg;

	while((evt_row = strsep(&row_ptr, ","))) {
		orig_evt_row=evt_row;
		field_evt = strsep(&evt_row, ":");
		evt_cfg = (event_cfg_t *)malloc(sizeof(event_cfg_t));
		evt_cfg->nr_counter = -1;
		evt_cfg->nr_exp = 0;
		strncpy(evt_cfg->name, field_evt, COMPLETE_EVENT_CFG_SIZE);
		evt_cfg->nr_properties = 0;
		key_field_evt = strsep(&field_evt, ".");
		value_field_evt = strsep(&field_evt, ".");

		/* Differences depending on whether the user specifies the "coretype" flag, an event code or mnemonic. */
		if (sscanf(orig_evt_row,"coretype=%d",&ind)==1) {
			if (ind <0 || ind>=max_core_types) {
				warnx("No such core type ID (%i)", ind);
				return -2;
			}

			coretype=ind;
			continue; /* Skip parsing flags */
		} else if(strncmp(key_field_evt, "0x", strlen("0x")) == 0) {
			strncpy(evt_cfg->code, key_field_evt, CODE_HW_EVENT_SIZE);
		} else {
			ind = subind = found = 0;
			subfound = 1;
			while(ind < pmu_info->nr_events && !found)
				if(strcmp(pmu_info->events[ind]->name, key_field_evt) == 0) {
					if(value_field_evt) {
						subfound = 0;
						while(subind < pmu_info->events[ind]->nr_subevents && !subfound)
							if(strcmp(pmu_info->events[ind]->subevents[subind]->name, value_field_evt) == 0) {
								subfound = 1;
							} else {
								subind++;
							}
					}
					found = 1;
				} else {
					ind++;
				}
			if(!found || !subfound) {
				warnx("Event '%s' or their subevent not found.", key_field_evt);
				return -2;
			}

			/* Point to event and subevent ... */
			evt_cfg->nr_counter = pmu_info->events[ind]->pmcn;
			cur_subevent=pmu_info->events[ind]->subevents[subind];
			strncpy(evt_cfg->code, pmu_info->events[ind]->code, CODE_HW_EVENT_SIZE);

			/* Copy properties for that event from pmu_info */
			for (x = 0; x < cur_subevent->nr_properties; x++) {
				pmc_property_t* cur_property = cur_subevent->properties[x];
				property_evt = (pmc_property_t *)malloc(sizeof(pmc_property_t));
				/* Copy everything */
				memcpy(property_evt, cur_property, sizeof(pmc_property_t));
				evt_cfg->properties[x] = property_evt;
				(evt_cfg->nr_properties)++;
			}

			if(pmu_info->events[ind]->pmcn > -1) {
				/*
				 * It is possible that in the experiment 0 is already used this fixed counter, should seek
				 * appropriate experiment.
				 */
				x = 0;
				for(ind = 0; ind < nr_events_cfg; ind++)
					if(events_cfg[ind]->nr_counter == evt_cfg->nr_counter)
						x++;
				evt_cfg->nr_exp = x;
				if(x > highest_exp)
					highest_exp = x;
				if(x == 0)
					pmc_mask |= (0x1 << evt_cfg->nr_counter);
			}
		}

		/* Process the specified flags on the user string for this event. */
		while((field_evt = strsep(&evt_row, ":"))) {
			key_field_evt = strsep(&field_evt, "=");
			value_field_evt = strsep(&field_evt, "=");
			if(strcmp(key_field_evt, "pmc") == 0 && evt_cfg->nr_counter == -1) {
				evt_cfg->nr_counter = atoi(value_field_evt);
				if(evt_cfg->nr_counter < pmu_info->nr_fixed_pmcs || evt_cfg->nr_counter >= pmu_info->nr_gp_pmcs) {
					warnx("Counter no.%i is not in the range of gp counters.", evt_cfg->nr_counter);
					return -3;
				}

				/*
				 * It is possible that in the experiment 0 is already used this gp counter, should seek
				 * appropriate experiment.
				 */
				x = 0;
				for(ind = 0; ind < nr_events_cfg; ind++)
					if(events_cfg[ind]->nr_counter == evt_cfg->nr_counter)
						x++;
				evt_cfg->nr_exp = x;
				if(x > highest_exp)
					highest_exp = x;
				if(x == 0)
					pmc_mask |= (0x1 << evt_cfg->nr_counter);
			} else if(strcmp(key_field_evt, "pmc") != 0) {
				x = 0;
				found = 0;
				/* Look if there is already a flag to update. */
				while(x < evt_cfg->nr_properties && !found) {
					if(strcmp(evt_cfg->properties[x]->key, key_field_evt) == 0) {
						if(value_field_evt)
							strncpy(evt_cfg->properties[x]->value, value_field_evt, VALUE_PMC_PROPERTY_SIZE);
						found = 1;
					}
					x++;
				}
				if(!found) { /* Create a new flag. */
					property_evt = (pmc_property_t *)malloc(sizeof(pmc_property_t));
					strncpy(property_evt->key, key_field_evt, NAME_KEY_PMC_PROPERTY_SIZE);
					if(value_field_evt)
						strncpy(property_evt->value, value_field_evt, VALUE_PMC_PROPERTY_SIZE);
					else
						property_evt->value[0] = '\0';
					evt_cfg->properties[evt_cfg->nr_properties] = property_evt;
					(evt_cfg->nr_properties)++;
				}
			}
		}

		events_cfg[nr_events_cfg] = evt_cfg;
		nr_events_cfg++;
	}

	/* Assign a counter to those events which have none assigned. */
	x = pmu_info->nr_fixed_pmcs;
	found = 1;
	ind = 0;
	actual_exp = 0;
	while(ind < nr_events_cfg) {
		if(events_cfg[ind]->nr_counter == -1) {
			while(x < (pmu_info->nr_fixed_pmcs + pmu_info->nr_gp_pmcs) && found) {
				subind = found = 0;
				while(subind < nr_events_cfg && !found) {
					if(events_cfg[subind]->nr_exp == actual_exp && events_cfg[subind]->nr_counter == x)
						found = 1;
					subind++;
				}
				if(found) x++;
			}
			if(found) {
				actual_exp++;
				x = pmu_info->nr_fixed_pmcs;
			} else {
				events_cfg[ind]->nr_counter = x;
				events_cfg[ind]->nr_exp = actual_exp;
				if(actual_exp == 0)
					pmc_mask |= (0x1 << x);
				ind++;
				found = 1;
			}
		} else {
			ind++;
		}
	}

	if(highest_exp > actual_exp) {
		nr_exps = highest_exp + 1;
	} else {
		nr_exps = actual_exp + 1;
	}

	/* Reserve into memory and generate the raw kernel strings. */
	for(ind = 0; ind < nr_exps; ind++) {
		raw_cfgs[ind] = (char *)malloc(ROW_CFG_SIZE*sizeof(char));
		raw_cfgs[ind][0] = '\0';
	}

	for(ind = 0; ind < (pmu_info->nr_fixed_pmcs + pmu_info->nr_gp_pmcs); ind++)
		mapping[ind].nr_counter = -1;

	for(ind = 0; ind < nr_events_cfg; ind++) {
		/* Update the mapping. */
		if(mapping[events_cfg[ind]->nr_counter].nr_counter == -1) {
			mapping[events_cfg[ind]->nr_counter].nr_counter = events_cfg[ind]->nr_counter;
			mapping[events_cfg[ind]->nr_counter].experiment_mask = 0;
		}
		mapping[events_cfg[ind]->nr_counter].events[events_cfg[ind]->nr_exp] = (char *)malloc(COMPLETE_EVENT_CFG_SIZE*sizeof(char));
		strncpy(mapping[events_cfg[ind]->nr_counter].events[events_cfg[ind]->nr_exp], events_cfg[ind]->name, COMPLETE_EVENT_CFG_SIZE);
		mapping[events_cfg[ind]->nr_counter].experiment_mask |= (0x1 << events_cfg[ind]->nr_exp);

		/* Generate corresponding kernel string. */
		if(raw_cfgs[events_cfg[ind]->nr_exp][0] != '\0')
			strcat(raw_cfgs[events_cfg[ind]->nr_exp], ",");

		if(events_cfg[ind]->nr_counter < pmu_info->nr_fixed_pmcs) {
			sprintf(row_cfg, "pmc%i", events_cfg[ind]->nr_counter);
		} else {
			sprintf(row_cfg, "pmc%i=%s", events_cfg[ind]->nr_counter, events_cfg[ind]->code);
		}
		strcat(raw_cfgs[events_cfg[ind]->nr_exp], row_cfg);

		for(subind = 0; subind < events_cfg[ind]->nr_properties; subind++) {
			if(events_cfg[ind]->properties[subind]->value[0] != '\0') {
				sprintf(row_cfg, ",%s%i=%s", events_cfg[ind]->properties[subind]->key, events_cfg[ind]->nr_counter, events_cfg[ind]->properties[subind]->value);
			} else {
				sprintf(row_cfg, ",%s%i", events_cfg[ind]->properties[subind]->key, events_cfg[ind]->nr_counter);
			}
			strcat(raw_cfgs[events_cfg[ind]->nr_exp], row_cfg);
		}
	}

	/* Append coretype flag if necessary */
	if (coretype!=-1)
		for(ind = 0; ind < nr_exps; ind++) {
			sprintf(row_cfg,",coretype=%i",coretype);
			strcat(raw_cfgs[ind],row_cfg);
		}

	/* Free the reserved memory on the execution of this function. */
	for(ind = 0; ind < nr_events_cfg; ind++) {
		for(subind = 0; subind < events_cfg[ind]->nr_properties; subind++)
			free(events_cfg[ind]->properties[subind]);
		free(events_cfg[ind]);
	}

	*nr_experiments = nr_exps;
	*used_counter_mask = pmc_mask;

	return 0;
}

/* Print a listing of the events supported by a given PMU */
void pmct_print_event_list(pmu_info_t *pmu_info, int verbose)
{
	int i, j, k = 0;

	for(i = 0; i < pmu_info->nr_events; i++) {
		for(j = 0; j < pmu_info->events[i]->nr_subevents; j++) {
			if(strncmp(pmu_info->events[i]->subevents[j]->name, "-", 1) != 0)
				printf("%s.%s", pmu_info->events[i]->name, pmu_info->events[i]->subevents[j]->name);
			else
				printf("%s", pmu_info->events[i]->name);
			if(verbose) {
				if(pmu_info->events[i]->pmcn < pmu_info->nr_fixed_pmcs)
					printf(" :: type=fixed");
				else
					printf(" :: type=gp");
				printf(", pmcn=%i, code=%s, flags={", pmu_info->events[i]->pmcn, pmu_info->events[i]->code);
				for(k = 0; k < pmu_info->events[i]->subevents[j]->nr_properties; k++) {
					if(k)
						printf(", ");
					printf("'%s':'%s'", pmu_info->events[i]->subevents[j]->properties[k]->key, pmu_info->events[i]->subevents[j]->properties[k]->value);
				}
				printf("}\n");
			} else {
				printf("\n");
			}

		}
	}
}

/* Print a summary of the properties of a PMU */
void pmct_print_pmu_info(pmu_info_t *pmu_info, int verbose)
{
	printf("pmu_model=%s\n", pmu_info->model);
	printf("nr_fixed_pmcs=%u\n", pmu_info->nr_fixed_pmcs);
	printf("nr_gp_pmcs=%u\n", pmu_info->nr_gp_pmcs);
}

/*
 * This function generates a summary indicating which physical
 * performance counter is used to hold the counts for the various
 * events on each multiplexing experiment.
 */
void pmct_print_counter_mappings(FILE* fout,
                                 counter_mapping_t* mappings,
                                 unsigned int pmcmask,
                                 unsigned int nr_experiments)
{
	unsigned long pmask=pmcmask;
	unsigned long emask;
	int i,j,k;

	for (i=0; i<MAX_PERFORMANCE_COUNTERS && pmask; i++) {
		/* Counter used for this experiment */
		if (pmask & 1<<i) {
			emask=mappings[i].experiment_mask;
			fprintf(fout,"pmc%d=",i);
			k=0;
			/* Iterate through used experiments for the counter ... */
			for (j=0; j<MAX_COUNTER_CONFIGS && emask; j++) {
				if (emask & 1<<j) {
					if (k>0)
						fprintf(fout,","); //Separator
					if (nr_experiments==1)
						fprintf(fout,"%s",mappings[i].events[j]);
					else
						fprintf(fout,"%s(%d)",mappings[i].events[j],j);

					emask &= ~(1<<j);
					k++;
				}
			}//for_j
			fprintf(fout,"\n");
			pmask &= ~(1<<i);
		}//endif
	}//for_i
}

/*
 * This function combines two mapping obtained by two calls to the
 * function pmct_parse_counter_string.
 */
static int merge_counter_mappings(counter_mapping_t* mapping_dst,
                                  counter_mapping_t* mapping_src,
                                  unsigned int pmcmask,
                                  unsigned int base_exp_idx)
{
	int i=0,j=0;
	unsigned long pmask=pmcmask;
	unsigned long emask;
	counter_mapping_t* cur_src;
	counter_mapping_t* cur_dst;

	while(pmask) {
		/* Counter used for this experiment */
		if (pmask & 1<<i) {
			cur_src=&mapping_src[i];
			cur_dst=&mapping_dst[i];
			emask=cur_src->experiment_mask;
			j=0;

			/* Iterate through used experiments for the counter ... */
			for (j=0; j<MAX_COUNTER_CONFIGS && emask; j++) {
				if (emask & 1<<j) {
					if (cur_dst->experiment_mask & (1<<(j+base_exp_idx))) {
						warnx("An existing experiment with the same ID was using the counter!!");
						return 1;
					}
					cur_dst->experiment_mask|=(1<<(j+base_exp_idx));
					cur_dst->events[j+base_exp_idx]=cur_src->events[j];
					/* Clean positions */
					emask &= ~(1<<j);
				}
			}
		}

		/* Clean positions */
		pmask &= ~(1<<i);
		i++;
	}

	return 0;
}

/*
 * This is a wrapper function for pmct_parse_counter_string(). It accepts
 * an array of PMC configuration strings in the RAW format or in the format
 * used by pmctrack command-line tool (mnemonic based).
 *
 * If the RAW format is used (non-zero "raw" parameter) this function
 * will create a copy of user_cfg_str in the output
 * parameter (raw_cfgs) and returns the number of experiments detected as well
 * as a bitmask with the number of PMCs used.
 *
 * If the input PMC configuration is specified in the pmctrack default format
 * (raw=0), this function will invoke pmct_parse_counter_string() as many times
 * as the number of strings in user_cfg_str. In this scenario, the return values
 * have the same meaning than those of pmct_parse_counter_string().
 */
int pmct_parse_pmc_configuration(const char* user_cfg_str[],
                                 int raw,
                                 int nr_coretype,
                                 char* raw_cfgs[],
                                 unsigned int *nr_experiments,
                                 unsigned int *used_counter_mask,
                                 counter_mapping_t *mapping)
{

	unsigned int i,j,k,succesful_cfgs;
	char* cpstring;
	int ret=0;
	unsigned long pmask,emask;

	if (raw) {
		i=0;
		while (user_cfg_str[i]) {
			cpstring=malloc(strlen(user_cfg_str[i])+1);
			strcpy(cpstring,user_cfg_str[i]);
			raw_cfgs[i]=cpstring;
			++i;
		}
		return 0;
	} else {
		/* Since it is not too big, reserve it in the stack for now*/
		struct {
			counter_mapping_t mappings[MAX_PERFORMANCE_COUNTERS];
			unsigned int pmcmask;
			unsigned int nr_exps; /* To know the experiments
																contained on each
																cfg string  */
		} aux_data[MAX_COUNTER_CONFIGS];
		int nr_actual_cfgs=0;
		char* tmp_raw_cfgs[MAX_COUNTER_CONFIGS];
		unsigned int base_exp_idx=0;
		unsigned int global_pmcmask;

		if (nr_coretype>=pmct_get_nr_pmus()) {
			warnx("No such PMU: %u\n",nr_coretype);
			return 1;
		}

		/* Default initialization */
		memset(aux_data,0,sizeof(aux_data));

		succesful_cfgs=0;

		for (i=0; user_cfg_str[i]!=NULL; i++,succesful_cfgs++) {
			if(pmct_parse_counter_string(user_cfg_str[i],
			                             nr_coretype,
			                             NULL,
			                             tmp_raw_cfgs,
			                             &aux_data[i].nr_exps,
			                             &aux_data[i].pmcmask,
			                             aux_data[i].mappings)!= 0) {
				//
				warnx("Error processing string:%s\n",user_cfg_str[i]);
				ret=1;
				goto free_resources;
			}

			if (nr_actual_cfgs+aux_data[i].nr_exps>MAX_COUNTER_CONFIGS) {
				warnx("Maximum number of experiments exceeded: %d>%d\n",
				      nr_actual_cfgs+aux_data[i].nr_exps,MAX_COUNTER_CONFIGS);
				ret=2;
				goto free_resources;
			}

			/* Accumulate raw strings */
			for (j=0; j<aux_data[i].nr_exps; j++)
				raw_cfgs[nr_actual_cfgs+j]=tmp_raw_cfgs[j];

			nr_actual_cfgs+=aux_data[i].nr_exps;
		}

		/*  Merge counter mappings */
		/* Clone the first one and merge the rest */
		memcpy(mapping,aux_data[0].mappings,sizeof(counter_mapping_t)*MAX_PERFORMANCE_COUNTERS);
		global_pmcmask=aux_data[0].pmcmask;
		base_exp_idx=aux_data[0].nr_exps;

		for (i=1; i<succesful_cfgs; i++) {
			if (merge_counter_mappings(mapping,
			                           aux_data[i].mappings,
			                           aux_data[i].pmcmask,
			                           base_exp_idx)) {
				ret=3;
				goto free_resources;
			}
			/* Update pmcmask as well */
			global_pmcmask|=aux_data[i].pmcmask;
			base_exp_idx+=aux_data[i].nr_exps;
		}

		/* Prepare remaining return values */
		(*used_counter_mask)=global_pmcmask;
		(*nr_experiments)=base_exp_idx;
		return 0;
free_resources:
		/* Free up resources */
		for (i=0; i<succesful_cfgs; i++) {
			pmask=aux_data[i].pmcmask;
			for (j=0; j<MAX_PERFORMANCE_COUNTERS && pmask; j++) {
				/* Counter used for this experiment */
				if (pmask & 1<<j) {
					emask=aux_data[i].mappings[j].experiment_mask;
					/* Iterate through used experiments for the counter ... */
					for (k=0; k<MAX_COUNTER_CONFIGS && emask; k++) {
						if (emask & 1<<k) {
							free(aux_data[i].mappings[j].events[k]);
							emask &= ~(1<<k);
						}
					}//for_k
					pmask &= ~(1<<j);
				}//endif
			}//for_j
		}//for_i
		return ret;
	}
}

/*
 * Prints to standard output the names of the virtual counters
 * available, one per line.
 */
void pmct_print_virtual_counter_list(void)
{
	virtual_counter_info_t *vci=pmct_get_virtual_counter_info();
	int i=0;

	if (vci) {
		for (i=0; i<vci->nr_virtual_counters; i++)
			printf("%s\n",vci->name[i]);
	}
}

/*
 * This function is responsible for obtaining the virtual counters
 * associated with the mnemonic string passed by argument.
 */
int __pmct_parse_vcounter_config(const char* virtcfg,
                                 unsigned int *virtual_mask,
                                 unsigned int* nr_virtual_counters,
                                 int* mnemonics_used)
{
	virtual_counter_info_t *vci=NULL; /* Do not load data for now */
	int virtmask=0;
	int counters=0;
	int idx,val;
	char cpbuf[MAX_CONFIG_STRING_SIZE+1];
	char* strconfig=NULL;
	char* flag=0;

	if (!virtcfg)
		return 1;

	/* Copy config in temporary storage */
	strconfig=cpbuf;
	strncpy(strconfig,virtcfg,MAX_CONFIG_STRING_SIZE+1);

	/* Parse configuration string to figure this out */
	while((flag = strsep(&strconfig, ","))!=NULL) {
		if((sscanf(flag,"virt%i=%x", &idx, &val))>0) {
			if (virtmask & (0x1<<idx)) {
				warnx("Virtual counter #%d was already selected\n",idx);
				return 1;
			}
			virtmask|=(0x1<<idx);
			counters++;
		} else {
			/* It's a mnemonic -> figure out ID */
			vci=pmct_get_virtual_counter_info();

			if (!vci) {
				warnx("Can't retrieve virtual counter information");
				return 1;
			}

			/* Search virtual counter */
			for (idx=0; idx<vci->nr_virtual_counters && strcmp(flag,vci->name[idx]); idx++) {}

			if (idx==vci->nr_virtual_counters) {
				warnx("Requested virtual counter is not available: %s\n",flag);
				return 1;
			}

			if (virtmask & (0x1<<idx)) {
				warnx("Virtual counter #%d was already selected (%s==virt%d)\n",idx,flag,idx);
				return 1;
			}

			/* Go ahead and select it */
			virtmask|=(0x1<<idx);
			counters++;
		}
	}

	/* Return values */
	(*virtual_mask)=virtmask;
	(*mnemonics_used)=(vci!=NULL);
	(*nr_virtual_counters)=counters;

	return 0;
}


/*
 * Print a listing of the virtual counters specified
 * in the "virtual_mask" bitmask
 * (Note: The behavior of this function depends on the current active
 *   monitoring module)
 */
void pmct_print_selected_virtual_counters(FILE* fout, unsigned int virtual_mask)
{
	virtual_counter_info_t *vci=pmct_get_virtual_counter_info();
	int i=0;
	unsigned int mask=virtual_mask;

	if (!vci)
		return;

	for (i=0; i<MAX_VIRTUAL_COUNTERS && mask ; i++) {
		if (mask & (0x1<<i)) {
			fprintf(fout,"virt%d=%s\n",i,vci->name[i]);
			mask&=~(0x1<<i);
		}
	}
}


/*
 * This function takes care of translating
 * a mnemonic-based virtual-counter configuration string
 * into a raw-formatted configuration string (kernel format).
 */
int pmct_parse_vcounter_config(const char* virtcfg,
                               unsigned int *virtual_mask,
                               unsigned int* nr_virtual_counters,
                               int* mnemonics_used,
                               char** raw_virtcfg)
{
	int len=0;
	int i=0;
	int cnt=0;
	unsigned int mask=0;
	char* dest=NULL;
	char* norm_cfg=0;

	if ( __pmct_parse_vcounter_config(virtcfg,
	                                  virtual_mask,
	                                  nr_virtual_counters,
	                                  mnemonics_used)) {
		warnx("Error parsing virtual counter configuration\n");
		return 1;
	}

	if (mnemonics_used) {

		/* Generate normalized string automatically */
		len= 1 /*'\0'*/ + 5*(*nr_virtual_counters) * /* virti */ +
		     ((*nr_virtual_counters)-1) /* , separators */;

		norm_cfg=malloc(len);
		mask=*virtual_mask;
		dest=norm_cfg;

		dest[0]='\0';

		for (i=0; i<MAX_VIRTUAL_COUNTERS && mask ; i++) {
			if (mask & (0x1<<i)) {
				if (cnt==0)
					dest+=sprintf(dest,"virt%d",i);
				else
					dest+=sprintf(dest,",virt%d",i);
				cnt++;
				mask&=~(0x1<<i);
			}
		}
	} else { /* Just copy string */
		norm_cfg=malloc(strlen(virtcfg)+1);
		strcpy(norm_cfg,virtcfg);
	}

	/* Update return values */
	(*raw_virtcfg)=norm_cfg;

	return 0;

}
