/*
 *  smart_power_driver.h
 *
 *  API to interact with the USB Driver for Odroid Smart Power
 *
 *  Copyright (c) 2016 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef SMART_POWER_H
#define SMART_POWER_H

/* Structure to represent a sample gathered by Odroid SmartPower */
struct spower_sample {
	int m_volts;
	int m_ampere;
	int m_watt;
	int m_watthour;
	int m_ujoules;
	unsigned long timestamp;
};

#ifdef CONFIG_SMART_POWER
/* Register USB driver and initialize global data structures */
int spower_register_driver(void);

/* Unregister USB driver and free up resources */
void spower_unregister_driver(void);

#else
static inline int spower_register_driver(void)
{
	return 0;
}
static inline void spower_unregister_driver(void) { }
#endif

/* Start gathering measurements from the device */
int spower_start_measurements(void);

/* Stop gathering measurements from the device */
void spower_stop_measurements(void);

/*
 * Get a sample that summarizes measurements collected from a given point
 * The function returns the number of samples in the buffer used to obtain
 * that sample (average).
 */
int spower_get_sample(unsigned long from, struct spower_sample* sample);

/* Operations to set/get the sampling period (specified in ms) */
int spower_set_sampling_period(unsigned int ms);
unsigned int spower_get_sampling_period(void);

/* Operations to manipulate the cummulative energy counter */
void spower_reset_energy_count(void);
uint64_t spower_get_energy_count(void);

#endif