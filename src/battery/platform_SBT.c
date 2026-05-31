/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>

#include <bluetooth/services/fast_pair/fmdn.h>

#include "app_battery.h"
#include "app_battery_priv.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(battery, LOG_LEVEL_DBG);

static const struct adc_dt_spec adc_chan0 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

#define BATTERY_POLL_THREAD_PRIORITY		K_PRIO_PREEMPT(0)
#define BATTERY_POLL_THREAD_STACK_SIZE		512

static int16_t adc_raw_data;

static struct adc_sequence adc_seq = {
		.buffer = &adc_raw_data,
		.buffer_size = sizeof(adc_raw_data),
	};

static uint16_t gu16_voltTable[15]={2900,2935,2970,3005,3040,3075,3110,3145,3180,3215,3250,3285,3320,3355,3390};
static uint8_t gu8_pergTable[15]={5,10,15,20,25,30,35,40,45,50,55,60,65,70,75};

static uint8_t battery_charge;
static K_SEM_DEFINE(poll_sem, 0, 1);

static int battery_init(void)
{
	int err=0;

	/* Check if device is ready */
	if (!adc_is_ready_dt(&adc_chan0)) {
		LOG_ERR("VDD ADC channel not ready");
		return -ENOENT;
	}

	/* Setup the channels once */
	err = adc_channel_setup_dt(&adc_chan0);
	if (err < 0) {
		LOG_ERR("Could not setup channel #%d (%d)", 0, err);
		return 0;
	}

	/* Initialize the ADC sequence */
	err = adc_sequence_init_dt(&adc_chan0, &adc_seq);
	if (err < 0) {
		LOG_ERR("Could not initalize sequnce");
		return 0;
	}

	return 0;
}

static int battery_voltage_get(int32_t *voltage)
{
	int err=0;
	int32_t val_mv;

	/* --- Read Channel --- */
	err = adc_read(adc_chan0.dev, &adc_seq);
	if (err) {
		LOG_ERR("Can't read ADC (err %d)", err);
		return err;
	}

	val_mv = adc_raw_data;

	/* Convert raw value to mV*/
	err = adc_raw_to_millivolts_dt(&adc_chan0, &val_mv);
	/* conversion to mV may not be supported, skip if not */
	if (err < 0) {
		LOG_WRN(" (value in mV not available)\n");
	} 

	LOG_INF("ADC count: %d, mV: %d", adc_raw_data, val_mv);

	*voltage = val_mv;

	return err;
}

/*
Battery Voltage to Percentage Convergen Table
*/
static uint8_t gu8_Volt2PercentTable(int32_t BatVolt)
{
	uint8_t lu8_i=15;
	
	while(lu8_i)
	{
		lu8_i--;
		
		if(BatVolt > gu16_voltTable[lu8_i])
		{
			return gu8_pergTable[lu8_i];
		}
	}
	
	return 0;
}


static uint8_t voltage_to_percent_convert(int32_t val)
{
	uint8_t per;

	if(val<3400)
	{
		per = gu8_Volt2PercentTable(val);
	}
	else
	{
		per = 100;
	}

	return (uint8_t) per;
}

static int battery_measure(uint8_t *charge)
{
	int err=0;
	int val_mv;

	/* Wait for voltage to stabilize */
	k_msleep(1);

	err = battery_voltage_get(&val_mv);
	if (err) {
		LOG_ERR("Can't read battery voltage (err %d)", err);
		return err;
	}

	*charge = voltage_to_percent_convert(val_mv);
	
	return err;
}

static int battery_level_set(bool forced_set)
{
	int err;
	static uint8_t battery_level;

	if (!forced_set && (battery_level == battery_charge)) {
		LOG_DBG("FMDN: battery level did not change");
		return 0;
	}

	err = bt_fast_pair_fmdn_battery_level_set(battery_charge);
	if (err) {
		LOG_ERR("FMDN: bt_fast_pair_fmdn_battery_level_set failed (err %d)",
			err);
		return err;
	}

	battery_level = battery_charge;

	app_battery_level_log(battery_level);

	return 0;
}

static void battery_level_set_work_handle(struct k_work *w)
{
	k_sem_give(&poll_sem);

	(void) battery_level_set(false);
}

static void battery_poll_thread_process(void)
{
	int err;

	static K_WORK_DEFINE(battery_level_set_work, battery_level_set_work_handle);

	while (1) {
		k_sem_take(&poll_sem, K_FOREVER);
		
		err = battery_measure(&battery_charge);
		if (err) {
			LOG_ERR("Battery measurement failed (err %d)", err);
		} else {
			LOG_INF("Measured battery charge: %d [%%]", battery_charge);
		}

		k_work_submit(&battery_level_set_work);

		k_sleep(K_SECONDS(CONFIG_APP_BATTERY_POLL_INTERVAL));
	}
}

K_THREAD_DEFINE(battery_poll_thread, BATTERY_POLL_THREAD_STACK_SIZE, battery_poll_thread_process,
		NULL, NULL, NULL, BATTERY_POLL_THREAD_PRIORITY, 0, 0);

static int battery_level_set_init(void)
{
	int err;

	err = battery_measure(&battery_charge);
	if (err) {
		return err;
	}

	err = battery_level_set(true);
	if (err) {
		return err;
	}

	return 0;
}

int app_battery_init(void)
{
	int err;

	err = battery_init();
	if (err) {
		LOG_ERR("Battery initialization failed (err %d)", err);
		return err;
	}

	err = battery_level_set_init();
	if (err) {
		LOG_ERR("Battery initialization level setting failed (err %d)", err);
		return err;
	}

	k_sem_give(&poll_sem);

	return 0;
}
