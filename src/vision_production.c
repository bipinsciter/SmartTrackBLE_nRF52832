/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <string.h>

#include "app_vision_production.h"
#include "app_nvs_storage.h"
#include "app_vision_advt.h"

/* Fallback definitions in case they aren't resolved out of app_nvs_storage.h */
#ifndef PRODUCTION_DATA_KEY
#define PRODUCTION_DATA_KEY  1
#endif
#ifndef CONFIG_DATA_KEY
#define CONFIG_DATA_KEY      2
#endif
#ifndef DYNAMIC_DATA_KEY
#define DYNAMIC_DATA_KEY     3
#endif

LOG_MODULE_REGISTER(vision_prod, LOG_LEVEL_INF);

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

static uint8_t rx_buf_1[UART_RX_BUF_SIZE];
static uint8_t rx_buf_2[UART_RX_BUF_SIZE];
static uint8_t tx_buf[UART_TX_BUF_SIZE];

/* Thread-safe storage for deferred background work queue processing */
static uint8_t deferred_rx_work_buf[UART_RX_BUF_SIZE];
static size_t deferred_rx_work_len = 0;

st_ProductionData_t gst_ProductionData;
st_ConfigData_t     gst_ConfigData;
st_DynamicData_t    gst_DynamicData;

static bool bool_ProductionDataWrite = false;
static bool bool_ConfigDataWrite = false;
static bool bool_DynamicDataWrite = false;
static bool uart_is_powered = false;
static bool rx_swap_toggle = false;

/* Delayed work structure for lifetime shutdown and deferred message processing */
static struct k_work_delayable uart_lifetime_timeout_work;
static struct k_work           uart_msg_processing_work;

K_MUTEX_DEFINE(uart_pm_mutex);

/* Forward declaration for thread processing functions */
static void deferred_msg_processing_handler(struct k_work *work);

/* -------------------------------------------------------------------- */
/* Asynchronous Driver Callback Routine                                 */
/* -------------------------------------------------------------------- */
static void uart_async_callback(const struct device *dev, 
                                struct uart_event *evt, 
                                void *user_data)
{
    switch (evt->type) {
    case UART_RX_RDY:
        if (!uart_is_powered) {
            break;
        }
        
        const uint8_t *incoming_ptr = evt->data.rx.buf + evt->data.rx.offset;
        size_t chunk_len = evt->data.rx.len;

        if (chunk_len > 0 && chunk_len <= UART_RX_BUF_SIZE) {
            k_mutex_lock(&uart_pm_mutex, K_FOREVER);
            
            /* Copy the data out of the active DMA buffer into secondary storage */
            memcpy(deferred_rx_work_buf, incoming_ptr, chunk_len);
            deferred_rx_work_len = chunk_len;
            
            /* Offload execution out of the critical ISR context to system threads */
            k_work_submit(&uart_msg_processing_work);
            
            k_mutex_unlock(&uart_pm_mutex);
        }
        break;

    case UART_RX_BUF_REQUEST:
        if (rx_swap_toggle) {
            uart_rx_buf_rsp(dev, rx_buf_1, UART_RX_BUF_SIZE);
        } else {
            uart_rx_buf_rsp(dev, rx_buf_2, UART_RX_BUF_SIZE);
        }
        rx_swap_toggle = !rx_swap_toggle;
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------- */
/* Lifetime Expiration Work Handler                                     */
/* -------------------------------------------------------------------- */
static void uart_lifetime_timeout_handler(struct k_work *work)
{
    app_uart_disable();
}

/* -------------------------------------------------------------------- */
/* Power-Managed API Implementations                                    */
/* -------------------------------------------------------------------- */

int app_uart_init(void)
{
    if (!device_is_ready(uart_dev)) {
        return -ENODEV;
    }

    int ret = uart_callback_set(uart_dev, uart_async_callback, NULL);
    if (ret != 0) {
        return ret;
    }

    k_work_init_delayable(&uart_lifetime_timeout_work, uart_lifetime_timeout_handler);
    k_work_init(&uart_msg_processing_work, deferred_msg_processing_handler);

    return 0;
}

int app_uart_enable(void)
{
    int ret = 0;
    k_mutex_lock(&uart_pm_mutex, K_FOREVER);

    if (uart_is_powered) {
        k_mutex_unlock(&uart_pm_mutex);
        return 0; 
    }
    
    ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);
    if (ret != 0 && ret != -EALREADY) {
        k_mutex_unlock(&uart_pm_mutex);
        return ret;
    }

    rx_swap_toggle = false;
    ret = uart_rx_enable(uart_dev, rx_buf_1, UART_RX_BUF_SIZE, 5000); 
    if (ret != 0) {
        pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
        k_mutex_unlock(&uart_pm_mutex);
        return ret;
    }

    uart_is_powered = true;
    k_mutex_unlock(&uart_pm_mutex);
    return 0;
}

int app_uart_disable(void)
{
    int ret = 0;
    k_mutex_lock(&uart_pm_mutex, K_FOREVER);

    if (!uart_is_powered) {
        k_mutex_unlock(&uart_pm_mutex);
        return 0; 
    }

    (void)uart_rx_disable(uart_dev);
    k_sleep(K_MSEC(10));

    ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
    if (ret == 0) {
        uart_is_powered = false;
    }

    k_mutex_unlock(&uart_pm_mutex);
    return ret;
}

int app_uart_transmit(const uint8_t *data, size_t len)
{
    int ret;
    k_mutex_lock(&uart_pm_mutex, K_FOREVER);

    if (!uart_is_powered) {
        k_mutex_unlock(&uart_pm_mutex);
        return -EACCES; 
    }

    ret = uart_tx(uart_dev, data, len, SYS_FOREVER_US);
    
    k_mutex_unlock(&uart_pm_mutex);
    return ret;
}

void app_uart_boot_sequence_start(void)
{
    if (app_uart_init() == 0) {
        if (app_uart_enable() == 0) {
            k_work_schedule(&uart_lifetime_timeout_work, K_SECONDS(UART_INIT_DISABLE_TIMEOUT_SEC));
        }
    }
}

uint8_t FindChecksum(const uint8_t *msg, uint16_t lu16_Count)
{
    uint32_t lu32_Total = 0;
    uint16_t lu16_i;
    
    for (lu16_i = 0; lu16_i < lu16_Count; lu16_i++) {
        lu32_Total += (uint32_t)*(msg + lu16_i);
    }
    lu32_Total = lu32_Total & 0xFF;
    lu32_Total = ((~lu32_Total) + 1) & 0xFF;
    
    return (uint8_t)(lu32_Total);
}

/* -------------------------------------------------------------------- */
/* Deferred Work Queue Message Processing (Thread Context)              */
/* -------------------------------------------------------------------- */
static void deferred_msg_processing_handler(struct k_work *work)
{
    uint8_t process_buf[UART_RX_BUF_SIZE];
    size_t process_len = 0;

    /* Safely pull data from the shared storage buffer */
    k_mutex_lock(&uart_pm_mutex, K_FOREVER);
    if (deferred_rx_work_len > 0) {
        process_len = deferred_rx_work_len;
        memcpy(process_buf, deferred_rx_work_buf, process_len);
        deferred_rx_work_len = 0; 
    }
    k_mutex_unlock(&uart_pm_mutex);

    if (process_len > 0) {
        ProcessProductionMsg(process_buf, process_len);
    }
}

void ProcessProductionMsg(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 2) {
        return;
    }

    uint8_t lu8_checksum = 0, lu8_i1 = 0;
    lu8_checksum = FindChecksum(data, len - 1);
    
    if (lu8_checksum == data[len - 1]) {
        k_work_reschedule(&uart_lifetime_timeout_work, K_MINUTES(UART_DISABLE_TIMEOUT_MIN));
        
        switch (data[UART_RX_MSG_COMMAND_IND]) 
        {
            case TOOL_RESET_CMD:
                tx_buf[0] = 3;
                tx_buf[1] = TOOL_RESET_CMD | 0x80;
                tx_buf[2] = SUCCESS;
                tx_buf[3] = FindChecksum(tx_buf, 3);
                app_uart_transmit(tx_buf, 4);
                break;
                
            case TOOL_PUT_DEEP_SLEEP_CMD:
                gst_ConfigData.mu8_DeepSleepControl = 0;
                bool_ConfigDataWrite = true;

                tx_buf[0] = 3;
                tx_buf[1] = TOOL_PUT_DEEP_SLEEP_CMD | 0x80;
                tx_buf[2] = SUCCESS;
                tx_buf[3] = FindChecksum(tx_buf, 3);
                app_uart_transmit(tx_buf, 4);        
                break;
                
            case TOOL_SET_DEFULT_PASSW:
                memset(gst_ProductionData.mu8ar_Password, 0, sizeof(gst_ProductionData.mu8ar_Password));
                memcpy(&gst_ProductionData.mu8ar_Password, &data[2], sizeof(gst_ProductionData.mu8ar_Password));
                bool_ProductionDataWrite = true;

                tx_buf[0] = 3;
                tx_buf[1] = data[UART_RX_MSG_COMMAND_IND] | 0x80;
                tx_buf[2] = SUCCESS;
                tx_buf[3] = FindChecksum(tx_buf, 3);
                app_uart_transmit(tx_buf, 4); 
                break;
                
            case TOOL_SET_SERIAL_OF_DUT_CMD:
                memset(gst_ProductionData.mu8_SerialNumber, 0, sizeof(gst_ProductionData.mu8_SerialNumber));
                memcpy(gst_ProductionData.mu8_SerialNumber, &data[2], sizeof(gst_ProductionData.mu8_SerialNumber));
                bool_ProductionDataWrite = true;  
            
                tx_buf[0] = 3;
                tx_buf[1] = TOOL_SET_SERIAL_OF_DUT_CMD | 0x80;
                tx_buf[2] = SUCCESS;
                tx_buf[3] = FindChecksum(tx_buf, 3);
                app_uart_transmit(tx_buf, 4);    
                break;
                
            case TOOL_SET_MAC_OF_DUT_CMD:
                for (lu8_i1 = 0; lu8_i1 < 6; lu8_i1++) {
                    gst_ProductionData.mu8ar_MACAddr[5 - lu8_i1] = data[lu8_i1 + 2];
                }   
                bool_ProductionDataWrite = true;  
                
                tx_buf[0] = 3;
                tx_buf[1] = TOOL_SET_MAC_OF_DUT_CMD | 0x80;
                tx_buf[2] = SUCCESS;
                tx_buf[3] = FindChecksum(tx_buf, 3);
                app_uart_transmit(tx_buf, 4);    
                break;
                    
            case TOOL_READ_SERIAL_OF_DUT_CMD:
                tx_buf[0] = 20;
                tx_buf[1] = TOOL_READ_SERIAL_OF_DUT_CMD | 0x80;             
                memcpy(&tx_buf[2], (uint8_t*)gst_ProductionData.mu8_SerialNumber, sizeof(gst_ProductionData.mu8_SerialNumber));
                tx_buf[20] = FindChecksum(tx_buf, 20);
                app_uart_transmit(tx_buf, 21);   
                break;
                
            case TOOL_READ_MAC_OF_DUT_CMD:
                tx_buf[0] = 8;
                tx_buf[1] = TOOL_READ_MAC_OF_DUT_CMD | 0x80;
                for (lu8_i1 = 0; lu8_i1 < 6; lu8_i1++) {
                    tx_buf[lu8_i1 + 2] = gst_ProductionData.mu8ar_MACAddr[5 - lu8_i1];
                }   
                tx_buf[8] = FindChecksum(tx_buf, 8);
                app_uart_transmit(tx_buf, 9);    
                break;
            
            case TOOL_READ_BLE_MAC_OF_DUT_CMD:					//Read public random static MAC of device
                
                tx_buf[0] = 8;
                tx_buf[1] = TOOL_READ_BLE_MAC_OF_DUT_CMD | 0x80;
                get_factory_mac_copy(&tx_buf[2]);
                tx_buf[8] = FindChecksum(tx_buf,8);
                app_uart_transmit(tx_buf,9);	
                
                break;

            case TOOL_SET_DUT_CONFG_CMD:
                memcpy((uint8_t*)&gst_ConfigData.mu16_AdvertismentInterval, &data[3], sizeof(gst_ConfigData.mu16_AdvertismentInterval));
                gst_ConfigData.mu8_TxPow = data[5];
                bool_ConfigDataWrite = true;  
            
                tx_buf[0] = 3;
                tx_buf[1] = TOOL_SET_DUT_CONFG_CMD | 0x80;
                tx_buf[2] = SUCCESS;
                tx_buf[3] = FindChecksum(tx_buf, 3);
                app_uart_transmit(tx_buf, 4);    
                break;

            case TOOL_READ_FW_VER_CMD:
                tx_buf[0] = 6;
                tx_buf[1] = TOOL_READ_FW_VER_CMD | 0x80;
                tx_buf[2] = FIRMWARE_MAJOR;
                tx_buf[3] = FIRMWARE_MINOR;
                tx_buf[4] = HARDWARE_MAJOR;
                tx_buf[5] = HARDWARE_MINOR;         
                tx_buf[6] = FindChecksum(tx_buf, 6);
                app_uart_transmit(tx_buf, 7);    
                break;

            case TOOL_SET_CLOCK_CMD:
                memcpy((uint8_t*)&gst_DynamicData.mu32_CurrentTime, &data[2], sizeof(gst_DynamicData.mu32_CurrentTime));
                memcpy((uint8_t*)&gst_ConfigData.s16_TimeZoneOffset, &data[6], sizeof(gst_ConfigData.s16_TimeZoneOffset));              
                bool_ConfigDataWrite = true;  
                bool_DynamicDataWrite = true;

                tx_buf[0] = 3;
                tx_buf[1] = TOOL_SET_CLOCK_CMD | 0x80;
                tx_buf[2] = SUCCESS;
                tx_buf[3] = FindChecksum(tx_buf, 3);
                app_uart_transmit(tx_buf, 4); 
                break;             
                
            case TOOL_SET_DEVICE_CONFIG:
                switch (data[UART_RX_MSG_SUB_COMMAND_IND]) 
                {
                    case 0:
                        memcpy(gst_ProductionData.mu8ar_ModelNumber, &data[3], sizeof(gst_ProductionData.mu8ar_ModelNumber));
                        bool_ProductionDataWrite = true;
                        tx_buf[0] = 4;
                        tx_buf[1] = TOOL_SET_DEVICE_CONFIG | 0x80;
                        tx_buf[2] = data[UART_RX_MSG_SUB_COMMAND_IND];
                        tx_buf[3] = SUCCESS;
                        tx_buf[4] = FindChecksum(tx_buf, 4);
                        app_uart_transmit(tx_buf, 5);       
                        break;
            
                    default:
                        tx_buf[0] = 4;
                        tx_buf[1] = TOOL_SET_DEVICE_CONFIG | 0x80;
                        tx_buf[2] = data[UART_RX_MSG_SUB_COMMAND_IND];
                        tx_buf[3] = INVALID_PROD_COMMAND;
                        tx_buf[4] = FindChecksum(tx_buf, 4);
                        app_uart_transmit(tx_buf, 5);       
                        break;
                }
                break;
                
            case TOOL_READ_DEVICE_CONFIG:
                switch (data[UART_RX_MSG_SUB_COMMAND_IND]) 
                {
                    case 0:
                        tx_buf[0] = 16;
                        tx_buf[1] = TOOL_READ_DEVICE_CONFIG | 0x80;
                        tx_buf[2] = data[UART_RX_MSG_SUB_COMMAND_IND];
                        tx_buf[3] = SUCCESS;
                        memcpy(&tx_buf[4], gst_ProductionData.mu8ar_ModelNumber, sizeof(gst_ProductionData.mu8ar_ModelNumber));
                        tx_buf[16] = FindChecksum(tx_buf, 16);
                        app_uart_transmit(tx_buf, 17);  
                        break;
                            
                    default:
                        tx_buf[0] = 4;
                        tx_buf[1] = TOOL_READ_DEVICE_CONFIG | 0x80;
                        tx_buf[2] = data[UART_RX_MSG_SUB_COMMAND_IND];
                        tx_buf[3] = INVALID_PROD_COMMAND;
                        tx_buf[4] = FindChecksum(tx_buf, 4);
                        app_uart_transmit(tx_buf, 5);       
                        break;
                }
                break;
                    
            default:
                break;
        }
    }

    /* Process NVS writes sequentially outside the ISR context */
    if (bool_ProductionDataWrite) {
        bool_ProductionDataWrite = false;
        LOG_INF("Production Para Updated");
        write_nvs_data(PRODUCTION_DATA_KEY, &gst_ProductionData, sizeof(st_ProductionData_t));
    }

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
}