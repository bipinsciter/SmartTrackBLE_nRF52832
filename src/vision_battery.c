/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <bluetooth/services/fast_pair/fmdn.h>

#include "app_battery.h"
#include "app_battery_priv.h"
#include "app_vision_production.h"
#include "app_nvs_storage.h"
#include "app_vision_bootup.h"
#include "app_vision_advt.h"

#include <zephyr/logging/log.h>
//LOG_MODULE_DECLARE(battery, LOG_LEVEL_DBG);
LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

static const struct adc_dt_spec adc_chan0 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

static int16_t adc_raw_data;
static struct adc_sequence adc_seq = {
    .buffer = &adc_raw_data,
    .buffer_size = sizeof(adc_raw_data),
};

static uint16_t gu16_voltTable[15] = {2900,2935,2970,3005,3040,3075,3110,3145,3180,3215,3250,3285,3320,3355,3390};
static uint8_t  gu8_pergTable[15]  = {5,10,15,20,25,30,35,40,45,50,55,60,65,70,75};

static uint8_t battery_charge;
static uint8_t gu8_ActBasedBattPercent;
static uint8_t gu8_LastActBasedBattPercent;

/* -------------------------------------------------------------------- */
/* Kernel Synchronization & Work Items (Moved to Global Scope)          */
/* -------------------------------------------------------------------- */
K_MUTEX_DEFINE(battery_mutex);

static struct k_work_delayable activity_battery_percentage_work;
static struct k_work_delayable battery_periodic_poll_work;
static struct k_work           battery_level_set_work;

/* Forward Declarations */
static int battery_measure(uint8_t *charge);
static int battery_level_set(bool forced_set);
static void battery_periodic_poll_handler(struct k_work *work);
static void battery_level_set_work_handler(struct k_work *work);

/* -------------------------------------------------------------------- */
/* ADC Low-Level Peripheral Drivers                                     */
/* -------------------------------------------------------------------- */
static int battery_init(void)
{
    int err = 0;

    if (!adc_is_ready_dt(&adc_chan0)) {
        LOG_ERR("VDD ADC channel hardware block not ready");
        return -ENOENT;
    }

    err = adc_channel_setup_dt(&adc_chan0);
    if (err < 0) {
        LOG_ERR("Could not setup channel (%d)", err);
        return err;
    }

    err = adc_sequence_init_dt(&adc_chan0, &adc_seq);
    if (err < 0) {
        LOG_ERR("Could not initialize sequence configuration");
        return err;
    }

    return 0;
}

static int battery_voltage_get(int32_t *voltage)
{
    int err = 0;
    int32_t val_mv;

    err = adc_read(adc_chan0.dev, &adc_seq);
    if (err) {
        LOG_ERR("Can't read ADC registers (err %d)", err);
        return err;
    }

    val_mv = adc_raw_data;
    err = adc_raw_to_millivolts_dt(&adc_chan0, &val_mv);
    if (err < 0) {
        LOG_WRN("Conversion to millivolts scaling not supported by hardware layout");
    } 

    LOG_DBG("ADC Counted Steps: %d, Calculated Voltage: %d mV", adc_raw_data, val_mv);
    *voltage = val_mv;

    updateAdvBatmV(val_mv);

    return 0;
}

static uint8_t gu8_Volt2PercentTable(int32_t BatVolt)
{
    uint8_t lu8_i = 15;
    
    while (lu8_i) {
        lu8_i--;
        if (BatVolt > gu16_voltTable[lu8_i]) {
            return gu8_pergTable[lu8_i];
        }
    }
    return 0;
}

static uint8_t voltage_to_percent_convert(int32_t val)
{
    uint8_t per;

    if (val < 3400) {
        per = gu8_Volt2PercentTable(val);
    } else {
        /* FIX: Wrapped shared variable read operation inside mutex protection */
        k_mutex_lock(&battery_mutex, K_FOREVER);
        per = gu8_ActBasedBattPercent;
        k_mutex_unlock(&battery_mutex);
    }

    return per;
}

static int battery_measure(uint8_t *charge)
{
    int err;
    int32_t val_mv;

    /* Give external power lines a micro window to settle */
    //k_msleep(1);
    k_busy_wait(100);

    err = battery_voltage_get(&val_mv);
    if (err) {
        LOG_ERR("Can't read battery voltage line (%d)", err);
        return err;
    }

    *charge = voltage_to_percent_convert(val_mv);

    updateAdvBatLevel(*charge);
    app_battery_level_log(*charge);

    return 0;
}

static int battery_level_set(bool forced_set)
{
    int err;
    static uint8_t battery_level = 0xFF;

    if (!forced_set && (battery_level == battery_charge)) {
        LOG_DBG("FMDN Payload: Battery level change negligible. Skipping set.");
        return 0;
    }

    err = bt_fast_pair_fmdn_battery_level_set(battery_charge);
    if (err) {
        LOG_ERR("FMDN Failure: Could not sync battery level to Google stack (err %d)", err);
        return err;
    }

    battery_level = battery_charge;

    return 0;
}

/* -------------------------------------------------------------------- */
/* Unified Work Queue Execution Handlers                                */
/* -------------------------------------------------------------------- */

static void battery_periodic_poll_handler(struct k_work *work)
{
    int err;

    err = battery_measure(&battery_charge);
    if (err) {
        LOG_ERR("Periodic battery evaluation failed (%d)", err);
    } else {
        LOG_INF("Volatge Based Percentage: %d %%", battery_charge);
    }

    /* Offload the FMDN status set smoothly out of the loop context */
    k_work_submit(&battery_level_set_work);

    /* Re-schedule the next periodic measurement cycle safely */
    k_work_schedule(&battery_periodic_poll_work, K_MINUTES(CONFIG_APP_BATTERY_VOLTAGE_POLL_INTERVAL));
}

static void battery_level_set_work_handler(struct k_work *work)
{
    /* Semaphore bounce removed. Straight processing pattern */
    (void) battery_level_set(false);
}

static void activity_battery_percentage_work_handler(struct k_work *work)
{
    k_mutex_lock(&battery_mutex, K_FOREVER);

    /* Reduce standby consumption parameters from the tracking capacity algorithm */
    gst_DynamicData.f32_RemainingBatCap -= AVG_DEEPSLEEP_CRNT_uAH;
    if (gst_DynamicData.f32_RemainingBatCap < 0.0f) {
        gst_DynamicData.f32_RemainingBatCap = 0.0f;
    }
    
    gu8_ActBasedBattPercent = (uint8_t)((gst_DynamicData.f32_RemainingBatCap / FULL_BAT_CAPACITY_uAH) * DEFAULT_BATT_PER);
    
    if (gu8_ActBasedBattPercent != gu8_LastActBasedBattPercent) {
        gu8_LastActBasedBattPercent = gu8_ActBasedBattPercent;

        k_mutex_unlock(&battery_mutex);

        write_nvs_data(DYNAMIC_DATA_KEY, &gst_DynamicData, sizeof(st_DynamicData_t));
    } else {
        k_mutex_unlock(&battery_mutex);
    }

    LOG_INF("ActivityBasedPercentage: %d %%", gu8_ActBasedBattPercent);

    k_work_schedule(&activity_battery_percentage_work, K_MINUTES(CONFIG_APP_BATTERY_PERCENTAGE_CAL_INTERVAL));
}

/* -------------------------------------------------------------------- */
/* Public API Implementations                                           */
/* -------------------------------------------------------------------- */

static int battery_level_set_init(void)
{
    int err;

    err = battery_measure(&battery_charge);
    if (err) return err;

    err = battery_level_set(true);
    if (err) return err;

    return 0;
}

int app_battery_init(void)
{
    int err;

    err = battery_init();
    if (err) {
        LOG_ERR("Battery driver hardware instantiation failed (%d)", err);
        return err;
    }

    err = battery_level_set_init();
    if (err) {
        LOG_ERR("Initial boot battery synchronization failed (%d)", err);
        return err;
    }

    /* 1. Initialize core system workqueue structural linkages */
    k_work_init_delayable(&activity_battery_percentage_work, activity_battery_percentage_work_handler);
    k_work_init_delayable(&battery_periodic_poll_work, battery_periodic_poll_handler);
    k_work_init(&battery_level_set_work, battery_level_set_work_handler);

    /* 2. Launch execution loops */
    k_work_schedule(&activity_battery_percentage_work, K_NO_WAIT);
    k_work_schedule(&battery_periodic_poll_work, K_NO_WAIT);

    return 0;
}
