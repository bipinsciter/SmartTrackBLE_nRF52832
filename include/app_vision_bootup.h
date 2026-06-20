/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_NVS_DATA_MANAGER_H_
#define APP_NVS_DATA_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_TIME				  			1780825185		//GMT: Sunday, June 7, 2026 at 9:39:45 AM
#define DEFAULT_TIMEZONE						330
#define DEFAULT_TX_POWER				        4	        // dBm
#define DEFAULT_ADV_INTERVAL			        2000		// msec
#define DEFAULT_ES_TX_POWER				        0			// dBm
#define DEFAULT_ES_INTERVAL				        5000		// msec

#define FULL_BAT_CAPACITY_uAH					2160000.0f
#define AVG_DEEPSLEEP_CRNT_uAH					14.0f
#define DEFAULT_BATT_PER					    100.0f


#define DEEP_SLEEP_ENABLE						0
#define DEEP_SLEEP_DISABLE						1
#define BAT_LVL_RST								2

#define DEFAULT_SRNUM				  	"SBC-ST104400090005"
#define DEFAULT_MAC						0x1C,0xCA,0xE3,0x20,0x00,0x05
#define DEFAULT_PASSWORD				"ins!gm@?"

/**
 * @brief Sweeps the NVS file system partitions to verify structural integrity.
 * * This function checks for specific unique "Magic Numbers" at the head of the 
 * Production, Configuration, and Dynamic data structures. 
 * - If a structural partition is blank, corrupted, or missing its magic number, 
 * hardcoded defaults are loaded into RAM and instantly flushed to flash.
 * - If valid data structures exist, they are extracted directly into global 
 * variables (`gst_ProductionData`, `gst_ConfigData`, `gst_DynamicData`) for 
 * runtime use.
 * - For normal operational reboots, it also automatically increments the 
 * device reset counter tracking metric.
 * * @note This function must be executed after mounting the underlying file system
 * via sh_nvs_handler_init() but before launching active BLE threads.
 * * @return 0 if the validation sweep succeeds, or a negative system errno on failure.
 */
int app_storage_verify_and_load(void);

/**
 * @brief Formats and outputs the complete storage array matrix to the serial log interface.
 */
void app_storage_dump_debug_logs(void);

void get_factory_mac_copy(uint8_t *dest_buf);

#ifdef __cplusplus
}
#endif

#endif /* APP_NVS_DATA_MANAGER_H_ */