/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

#include "app_vision_custom_services.h"
#include "app_vision_advt.h"
#include "app_vision_production.h"
#include "app_vision_bootup.h"
#include "app_nvs_storage.h"
#include "app_vision_time_manager.h"
#include "app_motion_detector.h"
#include "app_dfu.h"

LOG_MODULE_REGISTER(ble_custom_svc, LOG_LEVEL_INF);

/* -------------------------------------------------------------------- */
/* Forward Declarations                                                 */
/* -------------------------------------------------------------------- */
static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
void exchange_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params);
static void ble_parse_and_reply_work_handler(struct k_work *work);

int ble_custom_service_send_notification(struct bt_conn *conn, const uint8_t *data, uint16_t len);
int ble_custom_service_initiate_disconnect(void);
void app_graceful_self_restart(void);

/* -------------------------------------------------------------------- */
/* 1. Private RAM Buffers & Synchronization Variables                   */
/* -------------------------------------------------------------------- */
static uint8_t write_buffer[CUSTOM_MAX_DATA_LEN];
static uint16_t write_len = 0;

static uint8_t notify_buffer[CUSTOM_MAX_DATA_LEN];
static uint16_t notify_len = 0;

static uint8_t active_local_id = 0xFF;

static bool is_notifying_enabled = false;
static bool bool_UserAuthorization = false;
static bool bool_ConfigDataWrite = false;
static bool bool_DynamicDataWrite = false;
static bool bool_AdvtIntervalUpdated = false;

static struct bt_conn *current_conn = NULL;
struct bt_gatt_exchange_params exchange_params;

K_MUTEX_DEFINE(svc_mutex);

/* Kernel Work Queue Structures */
K_WORK_DEFINE(parse_and_reply_work, ble_parse_and_reply_work_handler);

#if IS_ENABLED(CONFIG_AUTHORIZATION_LOGIC_ENABLED)
static struct k_work_delayable authorisation_timout_work;
#endif

#if IS_ENABLED(CONFIG_BLE_CONNECTION_TIMEOUT_ENABLED)
static struct k_work_delayable connection_timout_work;
#endif

/* -------------------------------------------------------------------- */
/* 2. GATT Callback Implementations                                     */
/* -------------------------------------------------------------------- */

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

    LOG_INF("Inbound Data Written: offset %d, len %d, total %d", offset, len, write_len);
    
    k_work_submit(&parse_and_reply_work);

    k_mutex_unlock(&svc_mutex);
    return len;
}

static void cccd_changed_cb(const struct bt_gatt_attr *attr, uint16_t value)
{
    k_mutex_lock(&svc_mutex, K_FOREVER);
    is_notifying_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notifications: %s", is_notifying_enabled ? "ENABLED" : "DISABLED");
    k_mutex_unlock(&svc_mutex);
}

/* -------------------------------------------------------------------- */
/* 3. GATT Service Tree Declaration                                     */
/* -------------------------------------------------------------------- */
BT_GATT_SERVICE_DEFINE(custom_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_CUSTOM_SERVICE),

    BT_GATT_CHARACTERISTIC(BT_UUID_CUSTOM_WRITE_CHAR,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, write_incoming_char, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_CUSTOM_NOTIFY_CHAR,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_notify_char, NULL, NULL),
                           
    BT_GATT_CCC(cccd_changed_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

#if IS_ENABLED(CONFIG_AUTHORIZATION_LOGIC_ENABLED)
static void authorisation_timout_work_handler(struct k_work *work)
{
    LOG_WRN("Authorization Timeout");
    ble_custom_service_initiate_disconnect();
}
#endif

#if IS_ENABLED(CONFIG_BLE_CONNECTION_TIMEOUT_ENABLED)
static void connection_timout_work_handler(struct k_work *work)
{
    LOG_INF("Connection inactivity timeout");
    ble_custom_service_initiate_disconnect();
}
#endif

/* -------------------------------------------------------------------- */
/* 4. Parsing and Reply Data Worker Execution Loop                      */
/* -------------------------------------------------------------------- */
static void ble_parse_and_reply_work_handler(struct k_work *work)
{
    uint8_t temp_buf[CUSTOM_MAX_DATA_LEN]={0};
    uint16_t current_data_len = 0;
    struct bt_conn *conn_to_use = NULL;
    
    /* FIX: Implement thread-isolated scratchpad buffers to eliminate read/write data races */
    uint8_t local_reply_buf[CUSTOM_MAX_DATA_LEN];
    uint16_t response_len = 0;
    
    bool bool_disconnect = false;
    bool bool_restart = false;

    k_mutex_lock(&svc_mutex, K_FOREVER);
    if (write_len > 0) {
        current_data_len = write_len;
        memcpy(temp_buf, write_buffer, current_data_len);
        write_len = 0; 
    }
    if (current_conn != NULL) {
        conn_to_use = bt_conn_ref(current_conn);
    }
    k_mutex_unlock(&svc_mutex);

    if (current_data_len == 0 || conn_to_use == NULL) {
        if (conn_to_use) bt_conn_unref(conn_to_use);
        return;
    }

    LOG_INF("Parsing BLE command frame payload. Length: %d", current_data_len);
    //LOG_HEXDUMP_INF(temp_buf, current_data_len, "Command Payload:");
    memset(local_reply_buf, 0, sizeof(local_reply_buf));
    
    #if IS_ENABLED(CONFIG_AUTHORIZATION_LOGIC_ENABLED)
    if (!bool_UserAuthorization) 
    {
        if (temp_buf[COMMAND_ID] != PROVIDE_PASSWORD) 
        {

            local_reply_buf[0] = FAIL;
            local_reply_buf[1] = AUTHORIZATION_FAIL;
            response_len = 2;
            bool_disconnect = true;

            k_work_cancel_delayable(&authorisation_timout_work);
        }    
        else
        {
            LOG_INF("Command Code Received: PROVIDE_PASSWORD");

            if (memcmp(&temp_buf[1], &gst_ProductionData.mu8ar_Password[0], MAX_PASSWORD_SIZE) == 0) {
                
                LOG_INF("Authentication Successfull");              
                bool_UserAuthorization = true;
                k_work_cancel_delayable(&authorisation_timout_work);      

                #if IS_ENABLED(CONFIG_BLE_CONNECTION_TIMEOUT_ENABLED)
                k_work_schedule(&connection_timout_work, K_SECONDS(DISCC_TIME_SEC));   
                #endif

                local_reply_buf[0] = SUCCESS;
                local_reply_buf[1] = FIRMWARE_MAJOR;
                local_reply_buf[2] = FIRMWARE_MINOR;  
                local_reply_buf[3] = 0xFF;
                local_reply_buf[4] = 0xFF;                
                local_reply_buf[12] = HARDWARE_MAJOR;
                local_reply_buf[13] = HARDWARE_MINOR;
                response_len = 14; 
            } else {
                local_reply_buf[0] = FAIL;
                local_reply_buf[1] = INVALID_PASSWD;  
                response_len = 2;
                bool_disconnect = true;
            }
        }
    }
    else
    #endif  
    {
        #if IS_ENABLED(CONFIG_BLE_CONNECTION_TIMEOUT_ENABLED)
        /* Postpone the connectivity timer window */
        k_work_reschedule(&connection_timout_work, K_SECONDS(DISCC_TIME_SEC));
        #endif

        switch (temp_buf[COMMAND_ID]) 
        {
            case SET_REAL_TIME_CLOCK:
                LOG_INF("Command Code Received: SET_REAL_TIME_CLOCK"); 
                memcpy((uint8_t*)&gst_DynamicData.mu32_CurrentTime, &temp_buf[1], sizeof(gst_DynamicData.mu32_CurrentTime));
                memcpy((uint8_t*)&gst_ConfigData.s16_TimeZoneOffset, &temp_buf[5], sizeof(gst_ConfigData.s16_TimeZoneOffset));        
                bool_ConfigDataWrite = true;
                bool_DynamicDataWrite = true;

                app_time_sync_set_utc(gst_DynamicData.mu32_CurrentTime);

                local_reply_buf[0] = SUCCESS;
                response_len = 1;
                break;

            case READ_CURRENT_TIME:
                LOG_INF("Command Code Received: READ_CURRENT_TIME"); 

                uint32_t live_epoch = app_time_get_utc_epoch();
                if (live_epoch != 0) {
                    local_reply_buf[0] = SUCCESS;
                    memcpy(&local_reply_buf[1], &live_epoch, sizeof(live_epoch));
                    response_len = 5;
                } else {
                    /* Device clock has not been initialized with a sync frame yet */
                    local_reply_buf[0] = FAIL;
                    local_reply_buf[1] = INVALID_RTC;
                    response_len = 2;
                }
                break;  

            case READ_ASSOCIATION_PARA:
                if (temp_buf[1] == 6) {
                    LOG_INF("Command Code Received: READ_TIME_ZONE"); 
                    local_reply_buf[0] = SUCCESS;
                    local_reply_buf[1] = 6;
                    memcpy(&local_reply_buf[2], (uint8_t*)&gst_ConfigData.s16_TimeZoneOffset, sizeof(gst_ConfigData.s16_TimeZoneOffset));        
                    response_len = 4;
                } else {
                    local_reply_buf[0] = FAIL;
                    local_reply_buf[1] = INVALID_SUB_COMMAND; 
                    response_len = 2;
                    bool_disconnect = true;
                }
                break;  

            case SET_ADV_PERIOD:
                LOG_INF("Command Code Received: SET_ADV_PERIOD");  
                uint16_t lu16_temp_var; 
                memcpy((uint8_t*)&lu16_temp_var, &temp_buf[1], sizeof(lu16_temp_var));
                if (lu16_temp_var >= 100 && lu16_temp_var <= 5000) {
                    gst_ConfigData.mu16_AdvertismentInterval = lu16_temp_var;

                    k_mutex_lock(&svc_mutex, K_FOREVER);
                    bool_AdvtIntervalUpdated = true;
                    k_mutex_unlock(&svc_mutex);

                    bool_ConfigDataWrite = true;    
                    local_reply_buf[0] = SUCCESS;
                } else {
                    local_reply_buf[0] = FAIL;
                    local_reply_buf[1] = INVALID_VALUE;
                    bool_disconnect = true;
                }
                response_len = (local_reply_buf[0] == SUCCESS) ? 1 : 2;
                break;

            #if IS_ENABLED(CONFIG_LIS3DH_SENSOR_ENABLED)

            case SET_SENSOR_THRESHOLD:  
                LOG_INF("Command Code Received: SET_SENSOR_THRESHOLD");
                gst_ConfigData.mu8_Movement_INT_THS = temp_buf[6];  
                gst_ConfigData.mu8_Movement_INT_TIME = temp_buf[7]; 
                bool_ConfigDataWrite = true;    
                local_reply_buf[0] = SUCCESS;

                lis3dh_update(gst_ConfigData.mu8_Movement_INT_THS, gst_ConfigData.mu8_Movement_INT_TIME);

                response_len = 1;
                break;

            case READ_SENSOR_THRESHOLD:
                LOG_INF("Command Code Received: READ_SENSOR_THRESHOLD"); 
                local_reply_buf[0] = SUCCESS;
                local_reply_buf[2] = gst_ConfigData.mu8_Movement_INT_THS;
                local_reply_buf[3] = gst_ConfigData.mu8_Movement_INT_TIME;
                memcpy(&local_reply_buf[4], (uint8_t*)&gst_ConfigData.mu16_AdvertismentInterval, sizeof(gst_ConfigData.mu16_AdvertismentInterval));
                response_len = 6;
                break;  

            #endif

            case SET_ENERGY_SAVE_TIME:
                if (temp_buf[1] == GLOBAL_ENERGY_SAVE_CONFIG1) {
                    LOG_INF("Command Code Received: GLOBAL_ENERGY_SAVE_CONFIG1");
                    memcpy((uint8_t*)&gst_ConfigData.energy_save_para.u16_StartMinutes, &temp_buf[2], sizeof(gst_ConfigData.energy_save_para.u16_StartMinutes));
                    memcpy((uint8_t*)&gst_ConfigData.energy_save_para.u16_EndMinutes, &temp_buf[4], sizeof(gst_ConfigData.energy_save_para.u16_EndMinutes));
                    bool_ConfigDataWrite = true;
                    local_reply_buf[0] = SUCCESS;
                    response_len = 1;
                } else {
                    local_reply_buf[0] = FAIL;
                    local_reply_buf[1] = INVALID_SUB_COMMAND; 
                    response_len = 2;
                    bool_disconnect = true;
                }
                break;

            case READ_ENERGY_SAVING_PARA:
                LOG_INF("Command Code Received: READ_ENERGY_SAVING_PARA"); 
                local_reply_buf[0] = SUCCESS;
                memcpy(&local_reply_buf[1], (uint8_t*)&gst_ConfigData.energy_save_para.u16_StartMinutes, sizeof(gst_ConfigData.energy_save_para.u16_StartMinutes));     
                memcpy(&local_reply_buf[3], (uint8_t*)&gst_ConfigData.energy_save_para.u16_EndMinutes, sizeof(gst_ConfigData.energy_save_para.u16_EndMinutes));     
                response_len = 5;
                break;  

            case RESTART_DEVICE:
                LOG_INF("Command Code Received: RESTART_DEVICE");      
                bool_disconnect = true;
                bool_restart = true;
                local_reply_buf[0] = SUCCESS;
                response_len = 1;
                break;

            case PUT_DEVICE_IN_OTA_DFU_MODE:                                            
                LOG_INF("Command Code Received: PUT_DEVICE_IN_OTA_DFU_MODE");
                local_reply_buf[0] = SUCCESS; 
                response_len = 1;
                bool_disconnect = true;
                //bool_restart = true;

                //app_dfu_enter_mode_custom();

                break;

            case READ_BLE_MAC_ADDR:
                if (temp_buf[1] == 1) {
                    LOG_INF("Command Code Received: READ_BLE_MAC_ADDR");   
                    local_reply_buf[0] = SUCCESS;
                    get_factory_mac_copy(&local_reply_buf[1]);
                    response_len = 7;
                } else {
                    local_reply_buf[0] = FAIL;
                    local_reply_buf[1] = INVALID_SUB_COMMAND; 
                    response_len = 2;
                    bool_disconnect = true;
                }
                break;
                    
            case READ_FIRMWARE_DETAIL:
                LOG_INF("Command Code Received: GET_FIRMWARE_DETAIL"); 
                local_reply_buf[0] = SUCCESS;
                local_reply_buf[1] = FIRMWARE_MAJOR;
                local_reply_buf[2] = FIRMWARE_MINOR;
                response_len = 3;
                break;

            case READ_DIAGNOSTIC_DATA:
                LOG_INF("Command Code Received: READ_DIAGNOSTIC_DATA");    
                local_reply_buf[0] = SUCCESS;
                memcpy(&local_reply_buf[1], (uint8_t*)&gst_DynamicData.mu16_ResetCnt, sizeof(gst_DynamicData.mu16_ResetCnt));       
                response_len = 1 + sizeof(gst_DynamicData.mu16_ResetCnt);
                break;
                
            default:
                local_reply_buf[0] = FAIL;
                local_reply_buf[1] = INVALID_SUB_COMMAND;
                response_len = 2;
                bool_disconnect = true;
                break;
        }
    }
    
    /* Pushes data to notify_buffer safely wrapped behind internal mutex allocations */
    if (response_len > 0) {
        (void)ble_custom_service_send_notification(conn_to_use, local_reply_buf, response_len);
    }

    bt_conn_unref(conn_to_use);

    /* Process physical storage operations outside critical radio loops */
    if (bool_ConfigDataWrite) {
        bool_ConfigDataWrite = false;
        LOG_INF("Config Para Updated");
        write_nvs_data(CONFIG_DATA_KEY, &gst_ConfigData, sizeof(st_ConfigData_t));
    }

    if (bool_DynamicDataWrite) {
        bool_DynamicDataWrite = false;
        LOG_INF("Dynamic Para Updated");
        write_nvs_data(DYNAMIC_DATA_KEY, &gst_DynamicData, sizeof(st_DynamicData_t));
    }

    /* FIX: Let notifications clear the air buffers safely before dropping link or rebooting */
    if (bool_disconnect || bool_restart) {
        #if IS_ENABLED(CONFIG_BLE_CONNECTION_TIMEOUT_ENABLED)
        k_work_cancel_delayable(&connection_timout_work);
        #endif

        k_sleep(K_MSEC(60)); 
    }

    if (bool_disconnect) {
        ble_custom_service_initiate_disconnect();
    }

    if (bool_restart) {
        app_graceful_self_restart();
    }
}

/* -------------------------------------------------------------------- */
/* 5. Public Management APIs                                            */
/* -------------------------------------------------------------------- */

void app_graceful_self_restart(void)
{
    LOG_INF("============================================");
    LOG_INF("    INITIATING GRACEFUL SYSTEM REBOOT       ");
    LOG_INF("============================================");

    #if defined(CONFIG_BT)
    (void)ble_custom_service_initiate_disconnect();
    #endif

    (void)app_uart_disable();
    k_sleep(K_MSEC(100));

    LOG_INF("System registers flushed. Executing hardware reset now.\n");
    k_sleep(K_MSEC(10)); 

    sys_reboot(SYS_REBOOT_COLD);
}

int ble_custom_service_initiate_disconnect(void)
{
    struct bt_conn *conn_to_disconnect = NULL;
    int rc;

    k_mutex_lock(&svc_mutex, K_FOREVER);
    if (current_conn != NULL) {
        conn_to_disconnect = bt_conn_ref(current_conn);
    }
    k_mutex_unlock(&svc_mutex);

    if (conn_to_disconnect == NULL) {
        return -ENOTCONN;
    }

    k_sleep(K_MSEC(20)); 
    rc = bt_conn_disconnect(conn_to_disconnect, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    
    if (rc) {
        LOG_ERR("bt_conn_disconnect failed: %d", rc);
    }

    bt_conn_unref(conn_to_disconnect);
    return rc;
}

void exchange_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
    if (!err) {
        LOG_INF("MTU exchange complete. New MTU: %d", bt_gatt_get_mtu(conn));
    }
}

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

    int rc = bt_conn_get_info(conn, &info);
    if (rc == 0) {
        active_local_id = info.id;
        LOG_INF("Phone %s connected via ID: %d", addr, active_local_id);

        if (active_local_id == getVisionId()) {
            exchange_params.func = exchange_func; 
            bt_gatt_exchange_mtu(conn, &exchange_params);

            bool_UserAuthorization = false;
            
            #if IS_ENABLED(CONFIG_AUTHORIZATION_LOGIC_ENABLED)
            /* CRITICAL FIX: Simply schedule the timers here; initialization happens at boot */
            k_work_schedule(&authorisation_timout_work, K_SECONDS(AUTHO_TOUT_TIME_SEC));
            #endif
        }
    } else {
        LOG_ERR("Failed to extract connection info (%d)", rc);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    bool need_interval_update = false;
    bool start_advt = false;

    k_mutex_lock(&svc_mutex, K_FOREVER);
    if (current_conn == conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    if (active_local_id == getVisionId()) {
        LOG_INF("Disconnected from Vision Frame (Reason: 0x%02x)", reason);
        active_local_id = 0xFF;

        /* Isolate and capture the flag state safely under the local service mutex */
        if (bool_AdvtIntervalUpdated) {
            need_interval_update = true;
            bool_AdvtIntervalUpdated = false;
        }

        start_advt = true;

        bool_UserAuthorization = false;
        #if IS_ENABLED(CONFIG_AUTHORIZATION_LOGIC_ENABLED)
        k_work_cancel_delayable(&authorisation_timout_work);
        #endif

        #if IS_ENABLED(CONFIG_BLE_CONNECTION_TIMEOUT_ENABLED)
        k_work_cancel_delayable(&connection_timout_work);
        #endif
    }
    k_mutex_unlock(&svc_mutex);

    /* Safe execution area: Free from local mutex locks to prevent deadlocking adv_mutex */
    if (need_interval_update) {
        (void)ble_adv_custom_update_interval(gst_ConfigData.mu16_AdvertismentInterval, 
                                             gst_ConfigData.mu16_AdvertismentInterval + 10);
    }

    /* Restarts the connectable advertisement set cleanly */
    if(start_advt) ble_adv_custom_start();
}

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
    
    #if IS_ENABLED(CONFIG_AUTHORIZATION_LOGIC_ENABLED)
    /* CRITICAL FIX: Safe unified initialization of delayable timers at boot */
    k_work_init_delayable(&authorisation_timout_work, authorisation_timout_work_handler);
    #endif

    #if IS_ENABLED(CONFIG_BLE_CONNECTION_TIMEOUT_ENABLED)
    k_work_init_delayable(&connection_timout_work, connection_timout_work_handler);
    #endif

    k_mutex_unlock(&svc_mutex);

    LOG_INF("Write and Notification Custom Service tree deployed successfully.");
    return 0;
}

int ble_custom_service_send_notification(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    if (conn == NULL || data == NULL || len > CUSTOM_MAX_DATA_LEN) {
        return -EINVAL;
    }

    k_mutex_lock(&svc_mutex, K_FOREVER);
    memcpy(notify_buffer, data, len);
    notify_len = len;

    if (!is_notifying_enabled) {
        k_mutex_unlock(&svc_mutex);
        return -EACCES;
    }
    k_mutex_unlock(&svc_mutex);

    uint16_t negotiated_mtu = bt_gatt_get_mtu(conn);
    if (negotiated_mtu < (len + 3)) {
        return -EACCES;
    }

    int rc = bt_gatt_notify(conn, &custom_svc.attrs[4], data, len);
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