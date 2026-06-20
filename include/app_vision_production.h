/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_VISION_PRODUCTION_H_
#define APP_VISION_PRODUCTION_H_

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Define sizing schemas matched to your specific hardware metrics */
#define UART_RX_BUF_SIZE                64
#define UART_TX_BUF_SIZE                32
#define BLE_GAP_ADDR_LEN                6
#define SR_NUM_SIZE                     18
#define MAX_PASSWORD_SIZE               16
#define MODEL_NUMBER_LEN                12

/* Timing constraints */
#define UART_INIT_DISABLE_TIMEOUT_SEC   30   
#define UART_DISABLE_TIMEOUT_MIN        2

/* Command definitions */
#define UART_RX_MSG_LEN_IND				0
#define UART_RX_MSG_COMMAND_IND         1
#define UART_RX_MSG_SUB_COMMAND_IND     2

#define TOOL_RESET_CMD                  0x01
#define TOOL_PUT_DEEP_SLEEP_CMD         0x08
#define TOOL_SET_DEFULT_PASSW           0x3A
#define TOOL_SET_SERIAL_OF_DUT_CMD      0x0A
#define TOOL_SET_MAC_OF_DUT_CMD         0x0C
#define TOOL_READ_SERIAL_OF_DUT_CMD     0x0B
#define TOOL_READ_MAC_OF_DUT_CMD        0x0D
#define TOOL_SET_DUT_CONFG_CMD          0x10
#define TOOL_READ_FW_VER_CMD            0x16
#define TOOL_SET_CLOCK_CMD              0x21
#define TOOL_SET_DEVICE_CONFIG          0x3C
#define TOOL_READ_DEVICE_CONFIG         0x3D
#define TOOL_READ_BLE_MAC_OF_DUT_CMD    0x0E



/* Status replies */
#define FAIL							0x00
#define SUCCESS							0x01
#define INVALID_PROD_COMMAND			0x02

/* Static Firmware Tags */
#define FIRMWARE_MAJOR                  1
#define FIRMWARE_MINOR                  0
#define HARDWARE_MAJOR                  1
#define HARDWARE_MINOR                  0

typedef struct __attribute__((__packed__)) {
    uint16_t u16_StartMinutes;
    uint16_t u16_EndMinutes;
    uint16_t mu16_EnergySavingAdvInterval;
    uint8_t  mu8_EnergySavingGlobalTxPow;
} energy_save_para_t;

typedef struct __attribute__((packed, aligned(4))) {
    uint16_t mu16_MagicNumber;
    uint8_t  mu8ar_MACAddr[BLE_GAP_ADDR_LEN];
    uint8_t  mu8_SerialNumber[SR_NUM_SIZE];
    uint8_t  mu8ar_Password[MAX_PASSWORD_SIZE];
    uint8_t  mu8ar_ModelNumber[MODEL_NUMBER_LEN];
} st_ProductionData_t;   

typedef struct __attribute__((packed, aligned(4))) {
    uint16_t mu16_MagicNumber;
    uint16_t mu16_AdvertismentInterval;
    uint8_t  mu8_TxPow;
    uint8_t  mu8_Movement_INT_THS;
    uint8_t  mu8_Movement_INT_TIME;
    uint8_t  mu8_DeepSleepControl;
    int16_t  s16_TimeZoneOffset;
    energy_save_para_t energy_save_para;
} st_ConfigData_t;

typedef struct __attribute__((packed, aligned(4))) {
    uint16_t mu16_MagicNumber;
    float   f32_RemainingBatCap;
    uint16_t mu16_ResetCnt;
    uint32_t mu32_CurrentTime;
} st_DynamicData_t;

/* Extern definitions to allow application cross-references */
extern st_ProductionData_t gst_ProductionData;
extern st_ConfigData_t     gst_ConfigData;
extern st_DynamicData_t    gst_DynamicData;

int app_uart_init(void);
int app_uart_enable(void);
int app_uart_disable(void);
int app_uart_transmit(const uint8_t *data, size_t len);
void app_uart_boot_sequence_start(void);
void ProcessProductionMsg(const uint8_t *data, size_t len);
uint8_t FindChecksum(const uint8_t *msg, uint16_t lu16_Count);

#endif /* APP_VISION_PRODUCTION_H_ */
