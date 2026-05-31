#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include "app_vision_advt.h"

LOG_MODULE_REGISTER(vision_advt, LOG_LEVEL_INF);

/* -------------------------------------------------------------------- */
/* 1. Static/Global Private Instances                                   */
/* -------------------------------------------------------------------- */
#define COMPANY_ID 0x0437

static struct ble_service_data_t active_svc_data;
static struct ble_mfg_data_t active_mfg_data;
static const uint8_t device_name[] = "SBT-ST12345678";

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
    BT_DATA(BT_DATA_SVC_DATA16, &active_svc_data, sizeof(struct ble_service_data_t)),
    /* Type 0xFF: Manufacturer Specific Data */
    BT_DATA(BT_DATA_MANUFACTURER_DATA, &active_mfg_data, sizeof(struct ble_mfg_data_t))
};

static const struct bt_data sd[] = {
    /* Type 0x09: Complete Local Name */
    BT_DATA(BT_DATA_NAME_COMPLETE, device_name, sizeof(device_name) - 1)
};

/* -------------------------------------------------------------------- */
/* 3. API Implementation                                                */
/* -------------------------------------------------------------------- */

static void connected(struct bt_conn *conn, uint8_t err)
{
    is_advertising = false;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{   
    ble_adv_custom_start();
}

/* =========================================================================
 * FIXED FOR NCS v3.2.4: Use standard BT_CONN_CB_DEFINE macro 
 * This natively registers the handles directly into the app stack.
 * ========================================================================= */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int getVisionId(void)
{
    return Vision_id;
}

int ble_adv_custom_init(void)
{
    int rc;

    // Apply generic base default states to fields on cold launch
    active_svc_data.service_uuid = 0x9E83; // custome Service data
    memset(active_svc_data.payload, 0x00, sizeof(active_svc_data.payload));
    
    active_mfg_data.company_id = sys_cpu_to_le16(COMPANY_ID);  // eBest Company Identifier
    memset(active_mfg_data.payload, 0x11, sizeof(active_mfg_data.payload));

    // 1. Turn on the Bluetooth stack
    rc = bt_enable(NULL);
    if (rc && rc != -EALREADY) {
        LOG_ERR("Bluetooth core enable failed (%d)", rc);
        return rc;
    }

    // Crete BT ID for Vision Custom frame
    bt_addr_le_t addr;
    bt_addr_le_from_str("F1:01:D3:C4:B5:A6", "random", &addr);

    //addr.type = BT_ADDR_LE_PUBLIC;

    Vision_id = bt_id_create(&addr, NULL); // id1 will usually be 0 or 1
	if (Vision_id < 0) {
		LOG_ERR("Failed to create ID (err %d)\n", Vision_id);
	}
	else
	{
		LOG_INF("created ID %d\n", Vision_id);
	}

    struct bt_le_adv_param adv_param =
	{
		.id = Vision_id,
		.sid = 1,
		.secondary_max_skip = 0,
		.options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
		.interval_min = 3200, // 160 * 0.625ms = 100ms
		.interval_max = 3216,
	};

    // 2. CREATE THE ADVERTISING SET EXPLICITLY
    // We use BT_LE_EXT_ADV_CONN to make it a connectable set
    rc = bt_le_ext_adv_create(&adv_param, NULL, &adv_set);
    if (rc) {
        LOG_ERR("Failed to create explicit advertising set (%d)", rc);
        return rc;
    }

    // 3. ASSIGN DATA TO THE SPECIFIC SET HANDLE
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
        LOG_ERR("Advertising set not initialized! Call init first.");
        k_mutex_unlock(&adv_mutex);
        return -EINVAL;
    }

    /* * Define parameters for starting the specific set:
     * timeout = 0 (advertise indefinitely)
     * num_events = 0 (no event limit constraint)
     */
    struct bt_le_ext_adv_start_param start_param = {
        .timeout = 0,
        .num_events = 0,
    };

    // Start advertising using our explicit set handle
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

int ble_adv_custom_update(const struct ble_service_data_t *svc_data, 
                          const struct ble_mfg_data_t *mfg_data)
{
    int rc = 0;
    k_mutex_lock(&adv_mutex, K_FOREVER);

    if (adv_set == NULL) {
        k_mutex_unlock(&adv_mutex);
        return -EINVAL;
    }

    // Update RAM structures safely
    if (svc_data != NULL) {
        memcpy(&active_svc_data, svc_data, sizeof(struct ble_service_data_t));
    }
    if (mfg_data != NULL) {
        memcpy(&active_mfg_data, mfg_data, sizeof(struct ble_mfg_data_t));
    }

    // Dynamically re-assign modified data payload to our active set handle
    rc = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (rc) {
        LOG_ERR("Failed updating explicit set data payload (%d)", rc);
    } else {
        LOG_DBG("Advertising set payload refreshed on-the-fly.");
    }

    k_mutex_unlock(&adv_mutex);
    return rc;
}
