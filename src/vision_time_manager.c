/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <time.h>

#include "app_vision_custom_services.h"
#include "app_vision_production.h"
#include "app_vision_time_manager.h"
#include "app_vision_advt.h"

LOG_MODULE_REGISTER(time_mgr, LOG_LEVEL_INF);

/* Magic token to verify if the retained RAM survived smoothly or contains garbage */
#define TIME_MAGIC_VALIDATED    0xDEADBEEF

/* Retention structure mapped to the uninitialized section of RAM */
struct retained_time_block {
    uint32_t u32_MagicMarker;
    uint32_t u32_LastSavedUtcEpoch;
    int64_t  s64_LastSavedUptimeMs;
};

static __attribute__((section(".noinit"))) struct retained_time_block static_retained_time;

/* Global System Synchronizer Variables kept in fast volatile RAM */
static int64_t  s64_BaseUtcEpoch = 0;       /* Base anchor Unix Epoch timestamp */
static int64_t  s64_BaseUptimeTicks = 0;    /* Hardware tick count at anchor registration */
static bool     bool_IsTimeSynchronized = false;

static struct k_work_delayable time_supervisor_work;

K_MUTEX_DEFINE(time_mutex);

/**
 * @brief Calibrates the software system clock using an incoming trusted master timestamp 
 * (e.g., received via a custom phone app write or GPS fix).
 */
void app_time_sync_set_utc(uint32_t u32_IncomingEpochSeconds)
{
    k_mutex_lock(&time_mutex, K_FOREVER);

    /* Grab the hardware system uptime ticks directly from the low-power RTC engine */
    s64_BaseUptimeTicks = k_uptime_ticks();
    s64_BaseUtcEpoch = (int64_t)u32_IncomingEpochSeconds;
    bool_IsTimeSynchronized = true;

    static_retained_time.u32_LastSavedUtcEpoch = app_time_get_utc_epoch();
    static_retained_time.s64_LastSavedUptimeMs = k_uptime_get();
    static_retained_time.u32_MagicMarker = TIME_MAGIC_VALIDATED;

    k_mutex_unlock(&time_mutex);

    LOG_INF("System Time Synchronized! Master Anchor UTC Epoch set to: %lld", s64_BaseUtcEpoch);
}

/**
 * @brief Computes the active real-time UTC Unix Epoch value on demand.
 */
uint32_t app_time_get_utc_epoch(void)
{
    k_mutex_lock(&time_mutex, K_FOREVER);
    
    if (!bool_IsTimeSynchronized) {
        k_mutex_unlock(&time_mutex);
        return 0; /* Returns zero if device hasn't received a clock sync frame yet */
    }

    int64_t current_ticks = k_uptime_ticks();
    int64_t elapsed_ticks = current_ticks - s64_BaseUptimeTicks;
    
    /* Safely convert raw hardware clocks to actual integer seconds fractions */
    uint32_t current_utc = (uint32_t)(s64_BaseUtcEpoch + (elapsed_ticks / sys_clock_hw_cycles_per_sec()));
    
    k_mutex_unlock(&time_mutex);
    return current_utc;
}

/**
 * @brief Extracts the number of minutes elapsed in the current local day, accounting 
 * for the configurable field Time Zone offset variable.
 */
uint16_t app_time_get_local_minutes_of_day(void)
{
    uint32_t current_utc = app_time_get_utc_epoch();
    if (current_utc == 0) {
        return 0;
    }

    /* 1. Apply field timezone offset constraints (provided in total minutes) */
    int64_t local_timestamp = (int64_t)current_utc + ((int64_t)gst_ConfigData.s16_TimeZoneOffset * 60);

    /* 2. Break down the localized timestamp to isolate minutes since midnight */
    uint32_t seconds_in_day = (uint32_t)(local_timestamp % 86400ULL);
    uint16_t local_minutes_of_day = (uint16_t)(seconds_in_day / 60);

    return local_minutes_of_day;
}

/**
 * @brief Evaluates whether the current local time falls into the energy saving window.
 */
bool app_time_is_within_energy_saving_window(void)
{
    if (!bool_IsTimeSynchronized) {
        return false;
    }

    uint16_t current_min = app_time_get_local_minutes_of_day();
    uint16_t start_min = gst_ConfigData.energy_save_para.u16_StartMinutes;
    uint16_t end_min = gst_ConfigData.energy_save_para.u16_EndMinutes;

    /* Handle standard disabled or unconfigured flag tags */
    if (start_min == 0xFFFF || end_min == 0xFFFF) {
        return false;
    }

    /* Condition 1: Safe handling for standard daytime intervals (e.g., 08:00 to 17:00) */
    if (start_min < end_min) {
        if (current_min >= start_min && current_min < end_min) {
            return true;
        }
    } 
    /* Condition 2: Handling for intervals that cross midnight (e.g., 22:00 to 06:00) */
    else {
        if (current_min >= start_min || current_min < end_min) {
            return true;
        }
    }

    return false;
}

static void time_supervisor_work_handler(struct k_work *work)
{
    /* 2. Reschedule this check to execute again in exactly 1 minute.
     * The device sleeps completely during this 60-second window. */
    k_work_schedule(&time_supervisor_work, K_SECONDS(60));

    if (bool_IsTimeSynchronized) {

        uint32_t current_live_time = app_time_get_utc_epoch();

        gst_DynamicData.mu32_CurrentTime = current_live_time;

        /* Continuously store a backup copy of the time parameters into retained RAM */
        static_retained_time.u32_LastSavedUtcEpoch = app_time_get_utc_epoch();
        static_retained_time.s64_LastSavedUptimeMs = k_uptime_get();
        static_retained_time.u32_MagicMarker = TIME_MAGIC_VALIDATED;
    }
    else{

        return;
    }

    static bool Advtflag = false;

    /* 1. Check if the eco power-saving parameters apply */
    if (app_time_is_within_energy_saving_window()) {
        
        LOG_INF("Device is inside Energy Saving frame window. Applying Eco profile rules.");

        if(!Advtflag)
        {
            //ble_adv_custom_stop();
            //app_ui_request_broadcast(APP_UI_REQUEST_FMDN_ADV_MODE_CHANGE);
            //ble_adv_custom_update_interval(gst_ConfigData.energy_save_para.mu16_EnergySavingAdvInterval, gst_ConfigData.energy_save_para.mu16_EnergySavingAdvInterval+10);
            //ble_adv_custom_start();

            Advtflag = true;
        }

    } else {

        if(Advtflag)
        {
            //ble_adv_custom_stop();
            //app_ui_request_broadcast(APP_UI_REQUEST_FMDN_ADV_MODE_CHANGE);
            //ble_adv_custom_update_interval(gst_ConfigData.mu16_AdvertismentInterval, gst_ConfigData.mu16_AdvertismentInterval+10);
            //ble_adv_custom_start();

            Advtflag = false;
        }
    }
}

void app_time_manager_boot_recover(void)
{
    k_mutex_lock(&time_mutex, K_FOREVER);

    if (static_retained_time.u32_MagicMarker == TIME_MAGIC_VALIDATED) {
        /* Success! The device restarted, but RAM remained intact.
         * Calculate an estimate of the time lost during the hardware reset sequence.
         * Typically, an nRF52 boot cycle takes roughly 50 to 150 milliseconds. */
        uint32_t estimated_boot_loss_sec = 1; 

        s64_BaseUtcEpoch = (int64_t)static_retained_time.u32_LastSavedUtcEpoch + estimated_boot_loss_sec;
        s64_BaseUptimeTicks = k_uptime_ticks();
        bool_IsTimeSynchronized = true;

        LOG_INF("-> Recovered time from retained RAM! Current UTC: %lld", s64_BaseUtcEpoch);
    } else {
        LOG_WRN("-> Retained RAM empty or invalid. Waiting for a clean phone app sync frame.");
        bool_IsTimeSynchronized = false;
    }

    k_mutex_unlock(&time_mutex);
}

void app_time_activities_init(void)
{
    app_time_manager_boot_recover();

    k_work_init_delayable(&time_supervisor_work, time_supervisor_work_handler);
    
    /* Fire off the first evaluation cycle */
    k_work_schedule(&time_supervisor_work, K_NO_WAIT);
}



