/*
 *  vexpress_sensors.h
 *
 * 	High-level API enabling to access energy consumption registers
 *  on the ARM Coretile Express TC2 board and the ARM Juno development board
 * 
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>     
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_VEXPRESS_SENSORS_H
#define PMC_VEXPRESS_SENSORS_H
#include <pmc/mc_experiments.h>

/* Opaque data type */
struct vexpress_sensor;
typedef struct vexpress_sensor vexpress_sensor_t;

#define MAX_VEXPRESS_ENERGY_SENSORS 3

/* 
 * Minimal set of counters to provide
 * basic support to read sensors
 */
typedef struct {
	uint64_t cur;
	uint64_t prev;
	uint64_t acum;
} vexpress_sensors_count_t;


static inline void initialize_vexpress_sensors_count(vexpress_sensors_count_t* count){
	count->cur=count->prev=count->acum=0;
}

/* Initialize sensor connecion */
int initialize_energy_sensors(vexpress_sensor_t* sensor_descriptors[],
							  const char* description[],
							  int* nr_sensors);

/* Free up resources associated with sensors */
int release_energy_sensors(vexpress_sensor_t* sensor_descriptors[],
							  int nr_sensors);

/* Read the current value of a specific sensor */
int raw_read_energy_sensor(vexpress_sensor_t* sensor, uint64_t* value);

/* 
 * Retrieve energy consumption readings from the sensors.
 * The reading is provided with respect to the last time 
 * sensors were checked (difference only).
 */
void do_read_energy_sensors(vexpress_sensor_t* sensor_descriptors[],
							vexpress_sensors_count_t* sensor_counts,
							int nr_sensors,
						 	int acum);

#endif