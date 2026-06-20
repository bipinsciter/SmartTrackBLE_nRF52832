/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdlib.h>

/* Application Include Headers */
#include "app_vision_production.h"
#include "app_nvs_storage.h"
#include "app_vision_bootup.h"

LOG_MODULE_REGISTER(nvs_mgr, LOG_LEVEL_INF);

/* Unique Hex Token Templates to validate Flash Integrity */
#define PRODUCTION_MAGIC_NUMBER   0x5650  /* "VP" - Vision Production */
#define CONFIG_MAGIC_NUMBER       0x5643  /* "VC" - Vision Configuration */
#define DYNAMIC_MAGIC_NUMBER      0x5644  /* "VD" - Vision Dynamic */

static uint8_t factory_mac[BLE_GAP_ADDR_LEN];

void get_factory_mac_copy(uint8_t *dest_buf)
{
    if (dest_buf != NULL) {
        memcpy(dest_buf, factory_mac, BLE_GAP_ADDR_LEN);
    }
}

/* -------------------------------------------------------------------- */
/* Debug Parameter Dump Utility                                         */
/* -------------------------------------------------------------------- */
void app_storage_dump_debug_logs(void)
{
    LOG_INF("=======================================================");
    LOG_INF("          STORAGE PARAMETERS DEBUG DUMP                ");
    LOG_INF("=======================================================");
    
    /* 1. Production Data Structure Dump */
    LOG_INF("[PRODUCTION DATA] Slot Key ID: %d, Struct Size: %d bytes", 
            PRODUCTION_DATA_KEY, sizeof(st_ProductionData_t));
    LOG_INF("  -> Magic Number  : 0x%04X (Expected: 0x%04X)", 
            gst_ProductionData.mu16_MagicNumber, PRODUCTION_MAGIC_NUMBER);
    LOG_INF("  -> MAC Address   : %02X:%02X:%02X:%02X:%02X:%02X",
            gst_ProductionData.mu8ar_MACAddr[0], gst_ProductionData.mu8ar_MACAddr[1],
            gst_ProductionData.mu8ar_MACAddr[2], gst_ProductionData.mu8ar_MACAddr[3],
            gst_ProductionData.mu8ar_MACAddr[4], gst_ProductionData.mu8ar_MACAddr[5]);
    
    /* Safely format strings to prevent runaway pointers if flash contains garbage trailing bytes */
    char temp_str[32];
    snprintf(temp_str, sizeof(temp_str), "%.*s", SR_NUM_SIZE, gst_ProductionData.mu8_SerialNumber);
    LOG_INF("  -> Serial Number : %s", temp_str);
    
    snprintf(temp_str, sizeof(temp_str), "%.*s", MAX_PASSWORD_SIZE, gst_ProductionData.mu8ar_Password);
    LOG_INF("  -> Password      : %s", temp_str);
    
    snprintf(temp_str, sizeof(temp_str), "%.*s", MODEL_NUMBER_LEN, gst_ProductionData.mu8ar_ModelNumber);
    LOG_INF("  -> Model Number  : %s", temp_str);
    
    LOG_INF("-------------------------------------------------------");

    /* 2. Configuration Data Structure Dump */
    LOG_INF("[CONFIGURATION DATA] Slot Key ID: %d, Struct Size: %d bytes", 
            CONFIG_DATA_KEY, sizeof(st_ConfigData_t));
    LOG_INF("  -> Magic Number   : 0x%04X (Expected: 0x%04X)", 
            gst_ConfigData.mu16_MagicNumber, CONFIG_MAGIC_NUMBER);
    LOG_INF("  -> Adv Interval   : %d ms", 
            gst_ConfigData.mu16_AdvertismentInterval);
    LOG_INF("  -> TX Power Level : 0x%02X (%d dBm equivalent)", 
            gst_ConfigData.mu8_TxPow, (int8_t)gst_ConfigData.mu8_TxPow);
    LOG_INF("  -> Motion Thresh  : 0x%02X", 
            gst_ConfigData.mu8_Movement_INT_THS);
    LOG_INF("  -> Motion Duration: 0x%02X ODR Samples", 
            gst_ConfigData.mu8_Movement_INT_TIME);
    LOG_INF("  -> Deep Sleep Ctrl: %d (%s)", 
            gst_ConfigData.mu8_DeepSleepControl, gst_ConfigData.mu8_DeepSleepControl ? "DEEP SLEEP DISABLE" : "DEEP SLEEP ENABLE");
    LOG_INF("  -> Time Zone Off  : %d minutes (GMT %+d:%02d)", 
            gst_ConfigData.s16_TimeZoneOffset, gst_ConfigData.s16_TimeZoneOffset / 60, abs(gst_ConfigData.s16_TimeZoneOffset % 60));

    /* Sub-Struct Energy Save Dump */
    LOG_INF("  [Energy Saving Profile Parameters]");
    LOG_INF("    -> Window Frame : %02d:%02d to %02d:%02d", 
            gst_ConfigData.energy_save_para.u16_StartMinutes / 60, gst_ConfigData.energy_save_para.u16_StartMinutes % 60,
            gst_ConfigData.energy_save_para.u16_EndMinutes / 60, gst_ConfigData.energy_save_para.u16_EndMinutes % 60);
    LOG_INF("    -> Eco Adv Int  : %d msec", gst_ConfigData.energy_save_para.mu16_EnergySavingAdvInterval);
    LOG_INF("    -> Eco Tx Power : 0x%02X (%d dBm)", 
            gst_ConfigData.energy_save_para.mu8_EnergySavingGlobalTxPow, (int8_t)gst_ConfigData.energy_save_para.mu8_EnergySavingGlobalTxPow);
    
    LOG_INF("-------------------------------------------------------");

    /* 3. Dynamic Telemetry Data Structure Dump */
    LOG_INF("[DYNAMIC DATA] Slot Key ID: %d, Struct Size: %d bytes", 
            DYNAMIC_DATA_KEY, sizeof(st_DynamicData_t));
    LOG_INF("  -> Magic Number  : 0x%04X (Expected: 0x%04X)", 
            gst_DynamicData.mu16_MagicNumber, DYNAMIC_MAGIC_NUMBER);
    
    /* Zephyr logger defaults to picolibc printing configurations. 
     * To print double/float values reliably, cast or expand out components if toolchain flags restrict float formatting strings. */
    LOG_INF("  -> Cell Capacity : %d%% (mAh)", (int)gst_DynamicData.f32_RemainingBatCap);
    LOG_INF("  -> HW Boot Count : %u successful power cycles", gst_DynamicData.mu16_ResetCnt);
    LOG_INF("  -> Internal Time : %u seconds epoch", gst_DynamicData.mu32_CurrentTime);
    
    LOG_INF("=======================================================");
}

/* -------------------------------------------------------------------- */
/* Local Default Configuration Initializers                             */
/* -------------------------------------------------------------------- */

uint8_t insigma_mac_info[6] = {DEFAULT_MAC};
static void load_factory_production_defaults(st_ProductionData_t *prod)
{
    prod->mu16_MagicNumber = PRODUCTION_MAGIC_NUMBER;

    //memset(prod->mu8ar_MACAddr, 0x00, BLE_GAP_ADDR_LEN);
    memcpy(prod->mu8ar_MACAddr, insigma_mac_info, BLE_GAP_ADDR_LEN);
    memcpy(prod->mu8_SerialNumber, DEFAULT_SRNUM, SR_NUM_SIZE);
    memset(prod->mu8ar_Password, 0, MAX_PASSWORD_SIZE);
    strncpy((char *)prod->mu8ar_Password, DEFAULT_PASSWORD, MAX_PASSWORD_SIZE - 1);
    memset(prod->mu8ar_ModelNumber, 0x00, MODEL_NUMBER_LEN);
}

static void load_field_config_defaults(st_ConfigData_t *config)
{
    config->mu16_MagicNumber = CONFIG_MAGIC_NUMBER;

    config->mu16_AdvertismentInterval = DEFAULT_ADV_INTERVAL;   /* 2000 ms  */
    config->mu8_TxPow = DEFAULT_TX_POWER;                       /* 4 dBm default */
    config->mu8_Movement_INT_THS = 10;                          /* ~250mg threshold */
    config->mu8_Movement_INT_TIME = 10;                         /* Minimal duration constraint */
    config->mu8_DeepSleepControl = DEEP_SLEEP_DISABLE;          /* Keep awake at startup */
    config->s16_TimeZoneOffset = DEFAULT_TIMEZONE;              /* GMT +5:30 (Minutes) */

    /* Energy saving parameter subsets */
    config->energy_save_para.u16_StartMinutes = 1320; /* 22:00 */
    config->energy_save_para.u16_EndMinutes = 360;    /* 06:00 */
    config->energy_save_para.mu16_EnergySavingAdvInterval = DEFAULT_ES_INTERVAL; /* 5000 ms */
    config->energy_save_para.mu8_EnergySavingGlobalTxPow = DEFAULT_ES_TX_POWER;  /* 0 dBm */
}

static void load_dynamic_runtime_defaults(st_DynamicData_t *dynamic)
{
    dynamic->mu16_MagicNumber = DYNAMIC_MAGIC_NUMBER;
    
    dynamic->f32_RemainingBatCap = FULL_BAT_CAPACITY_uAH; /* 100% full capacity cell */
    dynamic->mu16_ResetCnt = 0;
    dynamic->mu32_CurrentTime = DEFAULT_TIME;
}

void read_raw_factory_mac(uint8_t *mac_out)
{
    /* Copy lower 32-bits */
    uint32_t addr_low = NRF_FICR->DEVICEADDR[0];
    /* Copy upper 16-bits */
    uint32_t addr_high = NRF_FICR->DEVICEADDR[1];

    mac_out[0] = (uint8_t)(addr_low & 0xFF);
    mac_out[1] = (uint8_t)((addr_low >> 8) & 0xFF);
    mac_out[2] = (uint8_t)((addr_low >> 16) & 0xFF);
    mac_out[3] = (uint8_t)((addr_low >> 24) & 0xFF);
    mac_out[4] = (uint8_t)(addr_high & 0xFF);
    /* For standard BLE compliance, enforce the two MSBs of index 5 as 11 */
    mac_out[5] = (uint8_t)((addr_high >> 8) & 0xFF) | 0xC0; 

    LOG_INF("Raw Hardware MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_out[5], mac_out[4], mac_out[3], mac_out[2], mac_out[1], mac_out[0]);
}


/* -------------------------------------------------------------------- */
/* Core Startup Verification Logic                                      */
/* -------------------------------------------------------------------- */

int app_storage_verify_and_load(void)
{
    int rc;

    LOG_INF("Checking Non-Volatile Storage for pre-existing configurations...");

    /* =========================================================================
     * 1. VERIFY PRODUCTION DATA LAYER
     * ========================================================================= */
    rc = read_nvs_data(PRODUCTION_DATA_KEY, &gst_ProductionData, sizeof(st_ProductionData_t));
    
    if (rc < 0 || gst_ProductionData.mu16_MagicNumber != PRODUCTION_MAGIC_NUMBER) {
        LOG_WRN("-> Production data slot missing or corrupted. Triggering first-time initialization...");
        
        /* Clear RAM and load factory hardcoded values */
        memset(&gst_ProductionData, 0, sizeof(st_ProductionData_t));
        load_factory_production_defaults(&gst_ProductionData);
        
        /* Commit directly to flash */
        rc = write_nvs_data(PRODUCTION_DATA_KEY, &gst_ProductionData, sizeof(st_ProductionData_t));
        if (rc < 0) {
            LOG_ERR("CRITICAL: Failed to save production default blocks (err: %d)", rc);
            return rc;
        }
        LOG_INF("Success: Default Factory Production Data initialized.");
    } else {
        LOG_INF("Success: Valid Factory Production Data loaded");
    }

    /* =========================================================================
     * 2. VERIFY FIELD CONFIGURATION LAYER
     * ========================================================================= */
    rc = read_nvs_data(CONFIG_DATA_KEY, &gst_ConfigData, sizeof(st_ConfigData_t));
    
    if (rc < 0 || gst_ConfigData.mu16_MagicNumber != CONFIG_MAGIC_NUMBER) {
        LOG_WRN("-> Device configuration slot empty. Deploying standard system profiles...");
        
        memset(&gst_ConfigData, 0, sizeof(st_ConfigData_t));
        load_field_config_defaults(&gst_ConfigData);
        
        rc = write_nvs_data(CONFIG_DATA_KEY, &gst_ConfigData, sizeof(st_ConfigData_t));
        if (rc < 0) {
            LOG_ERR("CRITICAL: Failed to save default user configuration parameters (err: %d)", rc);
            return rc;
        }
        LOG_INF("Success: Standard System Operational Profiles initialized.");
    } else {
        LOG_INF("Success: Valid User Configuration parameters loaded.");
    }

    /* =========================================================================
     * 3. VERIFY DYNAMIC TELEMETRY LAYER
     * ========================================================================= */
    rc = read_nvs_data(DYNAMIC_DATA_KEY, &gst_DynamicData, sizeof(st_DynamicData_t));
    
    if (rc < 0 || gst_DynamicData.mu16_MagicNumber != DYNAMIC_MAGIC_NUMBER) {
        LOG_WRN("-> Telemetry registry empty. Generating initial baseline context states...");
        
        memset(&gst_DynamicData, 0, sizeof(st_DynamicData_t));
        load_dynamic_runtime_defaults(&gst_DynamicData);
        
        rc = write_nvs_data(DYNAMIC_DATA_KEY, &gst_DynamicData, sizeof(st_DynamicData_t));
        if (rc < 0) {
            LOG_ERR("CRITICAL: Failed to commit dynamic tracking baselines (err: %d)", rc);
            return rc;
        }
        LOG_INF("Success: Dynamic Baseline Context States initialized.");
    } else {
        /* Increment reset counter since valid historical data exists */
        gst_DynamicData.mu16_ResetCnt++;
        
        LOG_INF("Success: Dynamic Context loaded.");
        
        /* Quietly update just the incremented reset count value back to flash cache */
        (void)write_nvs_data(DYNAMIC_DATA_KEY, &gst_DynamicData, sizeof(st_DynamicData_t));
    }

    /* Execute the dump profile print immediately after successful verification and loading */
    app_storage_dump_debug_logs();
    read_raw_factory_mac(&factory_mac[0]);

    LOG_INF("All device configuration profiles verified. Proceeding to application launch.");
    return 0;
}

