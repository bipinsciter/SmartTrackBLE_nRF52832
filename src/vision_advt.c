/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include "app_vision_advt.h"
#include "app_vision_production.h"

LOG_MODULE_REGISTER(vision_advt, LOG_LEVEL_INF);

/* -------------------------------------------------------------------- */
/* 1. Static/Global Private Instances                                   */
/* -------------------------------------------------------------------- */
#define COMPANY_ID 0x0F2C       //Vision Group Inc.

/* FIX: Unified into a single source-of-truth active structure block */
static ble_service_data_t active_svc_data;
static ble_mfg_data_t     active_mfg_data;
static uint8_t device_name[SR_NUM_SIZE+1] = "SBT-ST101212345678";

// Create BT ID for Vision Custom frame
bt_addr_le_t VisionMACaddr;
int Vision_id = -1;

K_MUTEX_DEFINE(adv_mutex);
static bool is_advertising = false;

// This handle replaces the legacy implicit setup
static struct bt_le_ext_adv *adv_set = NULL;

/* -------------------------------------------------------------------- */
/* 2. Bluetooth Packet Arrays (AD & SD Structures)                      */
/* -------------------------------------------------------------------- */

static struct bt_data ad[] = {
    /* Flags: General Discoverable, BR/EDR Not Supported */
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* Type 0x16: Service Data */
    BT_DATA(BT_DATA_SVC_DATA16, &active_svc_data, sizeof(ble_service_data_t)),
    /* Type 0xFF: Manufacturer Specific Data */
    BT_DATA(BT_DATA_MANUFACTURER_DATA, &active_mfg_data, sizeof(ble_mfg_data_t))
};

static struct bt_data sd[] = {
    /* Type 0x09: Complete Local Name */
    BT_DATA(BT_DATA_NAME_COMPLETE, device_name, 0)
};

/* -------------------------------------------------------------------- */
/* 3. API Implementation                                                */
/* -------------------------------------------------------------------- */

static void connected(struct bt_conn *conn, uint8_t err)
{
    k_mutex_lock(&adv_mutex, K_FOREVER);
    is_advertising = false;
    k_mutex_unlock(&adv_mutex);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{   
    LOG_INF("Disconnected from custom frame handler client link.");
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int getVisionId(void)
{
    return Vision_id;
}

/**
 * @brief Allocates and recovers the dedicated, separate Bluetooth Identity 
 * slot for the custom telemetry broadcast frame.
 * @return 0 on success, negative error code on failure.
 */
int app_vision_custom_id_create(void)
{
    size_t active_identities_count = 0;
    int ret = 0;

    /* 1. Query the controller to see how many slots are currently active/restored */
    bt_id_get(NULL, &active_identities_count);

    /* 2. Determine if your custom frame can reuse an existing slot, 
     * or if it needs to claim a brand new runtime handle.
     * For example, if you want it to always be the 3rd registered identity: */
    if (active_identities_count >= CONFIG_BT_ID_MAX) {
        /* If 3 or more profiles exist, your custom frame occupies index slot 2 */
        Vision_id = 2;
        LOG_INF("Custom frame reusing restored identity slot index: %d", Vision_id);
    } 
    else {
        /* Create a new identity. The stack will safely return the next 
         * available integer index slot (0, 1, or 2) depending on what's free. */
        ret = bt_id_create(NULL, NULL);
        if (ret < 0) {
            LOG_ERR("Failed to allocate a runtime identity slot (err %d)", ret);
            return ret;
        }
        
        Vision_id = ret;
        LOG_INF("Dynamically assigned unique Identity slot index: %d", Vision_id);
    }

    /* 3. Extract the MAC address safely linked to your dynamic slot handle */
    size_t macro_count = 1;
    bt_id_get(&VisionMACaddr, &macro_count);

    return 0;
}

int ble_adv_custom_init(void)
{
    int rc;

    // Apply generic base default states to fields on cold launch
    active_svc_data.service_uuid = 0x9E83; 
    memset(&active_svc_data.data, 0x00, sizeof(active_svc_data.data));
    active_svc_data.data.sp.FWVer = (uint8_t)(((FIRMWARE_MAJOR & 0x0F) << 4) | (FIRMWARE_MINOR & 0x0F));

    active_mfg_data.company_id = sys_cpu_to_le16(COMPANY_ID);
    memcpy(active_mfg_data.payload, gst_ProductionData.mu8ar_MACAddr, sizeof(gst_ProductionData.mu8ar_MACAddr));

    memset(device_name, 0, sizeof(device_name));
    snprintf((char *)device_name, sizeof(device_name), "%.*s", SR_NUM_SIZE, gst_ProductionData.mu8_SerialNumber);
    
    sd[0].data_len = strlen((char *)device_name);

    rc = bt_enable(NULL);
    if (rc && rc != -EALREADY) {
        LOG_ERR("Bluetooth core enable failed (%d)", rc);
        return rc;
    }

    rc = app_vision_custom_id_create();
    if (rc) {
        LOG_ERR("Identity slot configuration failed (%d)", rc);
        return rc;
    }

    struct bt_le_adv_param adv_param = {
        .id = Vision_id,  /* Resolves explicitly to slot index 2 */
        .sid = 2,         /* Set unique Advertising SID token wrapper */
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
        .interval_min = 3200, // 2000 ms
        .interval_max = 3216, // 2010 ms
    };

    rc = bt_le_ext_adv_create(&adv_param, NULL, &adv_set);
    if (rc) {
        LOG_ERR("Failed to create explicit advertising set (%d)", rc);
        return rc;
    }

    rc = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (rc) {
        LOG_ERR("Failed to assign data payload to advertising set (%d)", rc);
        return rc;
    }

    LOG_INF("Bluetooth context and explicit advertising set initialized.");
    return 0;
}

int ble_adv_custom_start(void)
{
    int rc = 0;
    k_mutex_lock(&adv_mutex, K_FOREVER);

    if (is_advertising) {
        k_mutex_unlock(&adv_mutex);
        return 0;
    }

    if (adv_set == NULL) {
        LOG_ERR("Advertising set not initialized!");
        k_mutex_unlock(&adv_mutex);
        return -EINVAL;
    }

    struct bt_le_ext_adv_start_param start_param = {
        .timeout = 0,
        .num_events = 0,
    };

    rc = bt_le_ext_adv_start(adv_set, &start_param);
    if (rc) {
        LOG_ERR("Failed to start explicit advertising set (%d)", rc);
    } else {
        is_advertising = true;
        LOG_INF("Connectable advertising set streaming launched.");
    }

    k_mutex_unlock(&adv_mutex);
    return rc;
}

int ble_adv_custom_stop(void)
{
    k_mutex_lock(&adv_mutex, K_FOREVER);
    
    if (!is_advertising || adv_set == NULL) {
        k_mutex_unlock(&adv_mutex);
        return 0;
    }

    int rc = bt_le_ext_adv_stop(adv_set);
    if (rc == 0) {
        is_advertising = false;
        LOG_INF("Advertising set streaming halted.");
    }

    k_mutex_unlock(&adv_mutex);
    return rc;
}

/* =========================================================================
 * GRACEFUL IMPROVEMENT: Protected, thread-safe direct verification update
 * ========================================================================= */
int ble_adv_custom_update(const ble_service_data_t *svc_data, 
                          const ble_mfg_data_t *mfg_data)
{
    int rc = 0;
    bool data_changed = false;

    /* Lock is re-entrant friendly if called internally or externally */
    k_mutex_lock(&adv_mutex, K_FOREVER);

    if (adv_set == NULL) {
        k_mutex_unlock(&adv_mutex);
        return -EINVAL;
    }

    /* 1. Evaluate incoming Service Data payload changes */
    if (svc_data != NULL) {
        if (memcmp(&active_svc_data, svc_data, sizeof(ble_service_data_t)) != 0) {
            memcpy(&active_svc_data, svc_data, sizeof(ble_service_data_t));
            data_changed = true;
        }
    }

    /* 2. Evaluate incoming Manufacturer Data payload changes */
    if (mfg_data != NULL) {
        if (memcmp(&active_mfg_data, mfg_data, sizeof(ble_mfg_data_t)) != 0) {
            memcpy(&active_mfg_data, mfg_data, sizeof(ble_mfg_data_t));
            data_changed = true;
        }
    }

    /* 3. Execute the over-the-air payload update ONLY if a variation was detected */
    if (data_changed) {
        rc = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
        if (rc) {
            LOG_ERR("Failed updating explicit set data payload (%d)", rc);
        } else {
            LOG_INF("Advertising set data dynamically updated over-the-air.");
        }
    } else {
        LOG_DBG("Update ignored: Packet payload values are unchanged.");
    }

    k_mutex_unlock(&adv_mutex);
    return rc;
}

/**
 * @brief Dynamically updates the custom advertisement interval parameters.
 * @param min_interval_ms New minimum interval in milliseconds (e.g., 100, 1000, 2000).
 * @param max_interval_ms New maximum interval in milliseconds.
 * @return 0 on success, negative error code on failure.
 */
int ble_adv_custom_update_interval(uint32_t min_interval_ms, uint32_t max_interval_ms)
{
    int rc = 0;

    /* 1. Convert milliseconds to standard BLE advertising ticks (1 tick = 0.625 ms) */
    uint16_t alt_interval_min = (uint16_t)(min_interval_ms / 0.625f);
    uint16_t alt_interval_max = (uint16_t)(max_interval_ms / 0.625f);

    k_mutex_lock(&adv_mutex, K_FOREVER);

    if (adv_set == NULL) {
        k_mutex_unlock(&adv_mutex);
        return -EINVAL;
    }

    /* 2. Temporarily halt the active broadcast stream */
    bool was_advertising = is_advertising;
    if (was_advertising) {
        rc = bt_le_ext_adv_stop(adv_set);
        if (rc) {
            LOG_ERR("Failed to halt advertising set for interval update (err %d)", rc);
            k_mutex_unlock(&adv_mutex);
            return rc;
        }
        is_advertising = false;
    }

    /* 3. Re-assign parameters on the fly to the explicit set handle */
    struct bt_le_adv_param dynamic_param = {
        .id = Vision_id,
        .sid = 1,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
        .interval_min = alt_interval_min,
        .interval_max = alt_interval_max,
    };

    rc = bt_le_ext_adv_update_param(adv_set, &dynamic_param);
    if (rc) {
        LOG_ERR("HCI Controller rejected the new advertising intervals (err %d)", rc);
        
        /* Rollback: Attempt to restart advertising with old parameters if it was running */
        if (was_advertising) {
            struct bt_le_ext_adv_start_param start_param = {0};
            (void)bt_le_ext_adv_start(adv_set, &start_param);
            is_advertising = true;
        }
        
        k_mutex_unlock(&adv_mutex);
        return rc;
    }

    /* 4. Relaunch streaming if the set was previously broadcasting */
    if (was_advertising) {
        struct bt_le_ext_adv_start_param start_param = {
            .timeout = 0,
            .num_events = 0,
        };

        rc = bt_le_ext_adv_start(adv_set, &start_param);
        if (rc) {
            LOG_ERR("Failed to resume advertising set after updating intervals (err %d)", rc);
        } else {
            is_advertising = true;
            LOG_INF("Advertising intervals successfully changed. New Min: %d ms, Max: %d ms", 
                    min_interval_ms, max_interval_ms);
        }
    } else {
        LOG_INF("Advertising parameters updated successfully while idle.");
    }

    k_mutex_unlock(&adv_mutex);
    return rc;
}

/* =========================================================================
 * FIXED: Direct struct access wrapped in atomic mutex boundaries
 * ========================================================================= */

void updateAdvBatLevel(uint8_t percent)
{
    k_mutex_lock(&adv_mutex, K_FOREVER);
    
    if (active_svc_data.data.sp.BatPercentage != percent) {
        active_svc_data.data.sp.BatPercentage = percent;
        
        /* Trigger the controller update directly using the updated layout */
        if (adv_set != NULL) {
            int rc = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
            if (!rc) {
                LOG_INF("Battery percentage updated in advertisement payload: %d%%", percent);
            }
        }
    }
    
    k_mutex_unlock(&adv_mutex);
}

void updateAdvBatmV(uint16_t mV)
{
    /* Perform shifting constraints cleanly */
    uint8_t calculated_bitfield = (((mV / 100) - 18) << 2) & 0x3C;

    k_mutex_lock(&adv_mutex, K_FOREVER);
    
    if (active_svc_data.data.sp.Bitfield3 != calculated_bitfield) {
        active_svc_data.data.sp.Bitfield3 = calculated_bitfield;
        
        if (adv_set != NULL) {
            int rc = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
            if (!rc) {
                LOG_INF("Battery voltage bitfield updated in advertisement payload: 0x%02X", calculated_bitfield);
            }
        }
    }
    
    k_mutex_unlock(&adv_mutex);
}
