#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "app_vision_custom_services.h"
#include "app_vision_advt.h"

LOG_MODULE_REGISTER(ble_custom_svc, LOG_LEVEL_INF);

/* -------------------------------------------------------------------- */
/* Forward Declarations                                                 */
/* -------------------------------------------------------------------- */
static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
void send_noti_work_handler(struct k_work *work);
void exchange_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params);

/* -------------------------------------------------------------------- */
/* 1. Private RAM Buffers & Synchronization Variables                  */
/* -------------------------------------------------------------------- */
static uint8_t write_buffer[CUSTOM_MAX_DATA_LEN];
static uint16_t write_len = 0;

static uint8_t notify_buffer[CUSTOM_MAX_DATA_LEN];
static uint16_t notify_len = 0;

static bool is_notifying_enabled = false;

static struct bt_conn *current_conn = NULL;
struct bt_gatt_exchange_params exchange_params;

K_MUTEX_DEFINE(svc_mutex);
K_WORK_DEFINE(notify_work, send_noti_work_handler);

/* -------------------------------------------------------------------- */
/* 2. GATT Callback Implementations                                     */
/* -------------------------------------------------------------------- */

// Called when the central client polls the Notify/Read characteristic manually
static ssize_t read_notify_char(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    ssize_t rc;
    k_mutex_lock(&svc_mutex, K_FOREVER);
    LOG_DBG("GATT Read on notification cache: offset %d, len %d", offset, notify_len);
    rc = bt_gatt_attr_read(conn, attr, buf, len, offset, notify_buffer, notify_len);
    k_mutex_unlock(&svc_mutex);
    return rc;
}

// Called when the central client pushes data to the Write characteristic
static ssize_t write_incoming_char(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len,
                                   uint16_t offset, uint8_t flags)
{
    k_mutex_lock(&svc_mutex, K_FOREVER);

    if (offset + len > CUSTOM_MAX_DATA_LEN) {
        k_mutex_unlock(&svc_mutex);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    memcpy(write_buffer + offset, buf, len);
    
    if (offset + len > write_len) {
        write_len = offset + len;
    }

    LOG_INF("Inbound Data Written: offset %d, len %d, total length %d", offset, len, write_len);

    k_mutex_unlock(&svc_mutex);
    return len;
}

// Tracking callback: Monitors if the smartphone activates/deactivates the CCCD toggle
static void cccd_changed_cb(const struct bt_gatt_attr *attr, uint16_t value)
{
    k_mutex_lock(&svc_mutex, K_FOREVER);
    is_notifying_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notifications: %s", is_notifying_enabled ? "ENABLED" : "DISABLED");
    k_mutex_unlock(&svc_mutex);
}

/* -------------------------------------------------------------------- */
/* 3. GATT Service Tree Declaration                                      */
/* -------------------------------------------------------------------- */

BT_GATT_SERVICE_DEFINE(custom_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_CUSTOM_SERVICE),

    // Characteristic 1: Strict WRITE ONLY Access 
    BT_GATT_CHARACTERISTIC(BT_UUID_CUSTOM_WRITE_CHAR,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, write_incoming_char, NULL),

    // Characteristic 2: NOTIFY Only Access
    BT_GATT_CHARACTERISTIC(BT_UUID_CUSTOM_NOTIFY_CHAR,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_notify_char, NULL, NULL),
                           
    // Client Characteristic Configuration Descriptor (CCCD).
    BT_GATT_CCC(cccd_changed_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* -------------------------------------------------------------------- */
/* 4. Public Management APIs                                            */
/* -------------------------------------------------------------------- */

void exchange_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
    if (!err) {
        LOG_INF("MTU exchange complete. New MTU: %d", bt_gatt_get_mtu(conn));
    }
}

static uint8_t active_local_id = 0xFF;

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;

    if (err) {
        LOG_ERR("Connection failed, err 0x%02x", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    k_mutex_lock(&svc_mutex, K_FOREVER);
    current_conn = bt_conn_ref(conn);
    k_mutex_unlock(&svc_mutex);

    
    /* =========================================================================
     * FIXED FOR NCS v3.2.4: Use bt_conn_get_id() to safely fetch the local identity
     * ========================================================================= */
    int rc = bt_conn_get_info(conn, &info);
    if (rc == 0) {
        active_local_id = info.id;

        LOG_INF("Phone %s connected using an alternate identity (ID: %d).", addr, active_local_id);

        if (active_local_id == getVisionId()) 
        {
            // Request a larger MTU immediately upon connection
            exchange_params.func = exchange_func; // Optional callback
            bt_gatt_exchange_mtu(conn, &exchange_params);
        }

        /*if (active_local_id == 1) {
            LOG_INF("Connected to phone %s using FP", addr);
        } else if (active_local_id == getVisionId()) {
            LOG_INF("Connected to phone %s using Vision", addr);
            // Request a larger MTU immediately upon connection
            exchange_params.func = exchange_func; // Optional callback
            bt_gatt_exchange_mtu(conn, &exchange_params);
        } else {
            LOG_INF("Phone %s connected using an alternate identity (ID: %d).", addr, active_local_id);
        }*/
    } else {
        LOG_ERR("Failed to safely extract connection metadata parameters (%d)", rc);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    k_mutex_lock(&svc_mutex, K_FOREVER);
    if (current_conn == conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    if (active_local_id == getVisionId()) 
    {
        LOG_INF("Disconnected from Vision Frame (Reason: 0x%02x)", reason);
        active_local_id = 0xFF;
    }

    k_mutex_unlock(&svc_mutex);
}

/* =========================================================================
 * FIXED FOR NCS v3.2.4: Use standard BT_CONN_CB_DEFINE macro 
 * This natively registers the handles directly into the app stack.
 * ========================================================================= */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int ble_custom_service_init(void)
{
    k_mutex_lock(&svc_mutex, K_FOREVER);
    memset(write_buffer, 0, sizeof(write_buffer));
    memset(notify_buffer, 0, sizeof(notify_buffer));
    write_len = 0;
    notify_len = 0;
    k_mutex_unlock(&svc_mutex);

    // Note: Manual registration function removed here because BT_CONN_CB_DEFINE handles it automatically at boot!

    LOG_INF("Write and Notification Custom Service tree deployed successfully.");
    return 0;
}

int ble_custom_service_send_notification(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    if (conn == NULL || data == NULL || len > CUSTOM_MAX_DATA_LEN) {
        return -EINVAL;
    }

    k_mutex_lock(&svc_mutex, K_FOREVER);
    
    // Update local cache array 
    memcpy(notify_buffer, data, len);
    notify_len = len;

    // Verify client has subscribed via CCCD toggle action
    if (!is_notifying_enabled) {
        k_mutex_unlock(&svc_mutex);
        LOG_DBG("Notification skipped: Client has not enabled subscriptions via CCCD.");
        return -EACCES;
    }

    k_mutex_unlock(&svc_mutex);

    uint16_t negotiated_mtu = bt_gatt_get_mtu(conn);
    if (negotiated_mtu < (CUSTOM_MAX_DATA_LEN + 3)) {
        LOG_DBG("MTU too small (%d). Need at least %d. Waiting for exchange...", 
                negotiated_mtu, CUSTOM_MAX_DATA_LEN + 3);
        return -EACCES;
    }

    int rc = bt_gatt_notify(conn, &custom_svc.attrs[4], data, len);
    if (rc) {
        LOG_ERR("Failed to push notification frame over-the-air (%d)", rc);
    } else {
        LOG_INF("240-byte notification frame pushed to air successfully.");
    }

    return rc;
}

int ble_custom_service_get_written_data(uint8_t *out_data, uint16_t *out_len)
{
    if (out_data == NULL || out_len == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&svc_mutex, K_FOREVER);
    if (write_len == 0) {
        k_mutex_unlock(&svc_mutex);
        return -ENODATA;
    }

    memcpy(out_data, write_buffer, write_len);
    *out_len = write_len;
    write_len = 0; 
    
    k_mutex_unlock(&svc_mutex);
    return 0;
}

bool is_char2_notification_enabled(struct bt_conn *conn)
{
    if (conn == NULL) {
        return false;
    }
    
    const struct bt_gatt_attr *attr = &custom_svc.attrs[4];
    return bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY);
}

void send_noti_work_handler(struct k_work *work)
{
    struct bt_conn *conn_to_use = NULL;

    k_mutex_lock(&svc_mutex, K_FOREVER);
    if (current_conn != NULL) {
        conn_to_use = bt_conn_ref(current_conn);
    }
    k_mutex_unlock(&svc_mutex);

    if (conn_to_use != NULL) {
        ble_custom_service_send_notification(conn_to_use, notify_buffer, CUSTOM_MAX_DATA_LEN);
        bt_conn_unref(conn_to_use);
    }
}