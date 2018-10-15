/*
*  include/pmc/data_str/phase_table.h
*
* 	Phase table generic data structure
*
*  Copyright (c) 2016 Juan Carlos Saez <jcsaezal@ucm.es>
*
*  This code is licensed under the GNU GPL v2.
*/

#ifndef PHASE_TABLE_H
#define PHASE_TABLE_H

struct phase_table;
typedef struct phase_table phase_table_t;

/* Create/Destroy a phase table */
phase_table_t* create_phase_table(unsigned int max_phases, size_t phase_struct_size, int (*compare)(void*,void*,void*));
void destroy_phase_table(phase_table_t* table);

/* Retrieve the most similar phase in a phase table */
void* get_phase_from_table(phase_table_t* table, void* key_phase, void* priv_data, int* similarity, int* index);

/* Move table entry in the index position to the beginning of the table */
int promote_table_entry(phase_table_t* table, int index);

/* Insert a new phase into the phase table */
void* insert_phase_in_table(phase_table_t* table, void* phase);

#endif