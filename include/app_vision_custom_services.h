#ifndef BLE_CUSTOM_SERVICE_H_
#define BLE_CUSTOM_SERVICE_H_

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------
#define DEEP_SLEEP_ENABLE						0
#define DEEP_SLEEP_DISABLE						1
#define BAT_LVL_RST								2
//---------------------------------------------------------
#define INVALID_PASSWD							1
#define INVALID_LEVEL							2
#define AUTHORIZATION_FAIL						3
#define INVALID_COMMAND							4	
#define INVALID_COMMAND_SQN_NUM					5
#define INVALID_EVENT_INDEX						6
#define INVALID_SUB_COMMAND						7
#define INVALID_VALUE							8
#define INVALID_FW								9
#define INVALID_RTC								10
//---------------------------------------------------------
#define AUTHO_TOUT_TIME_SEC						20
#define DISCC_TIME_SEC     						60
//---------------------------------------------------------
#define CUSTOM_MAX_DATA_LEN     240
#define	COMMAND_ID      		0
//---------------------------------------------------------
#define PROVIDE_PASSWORD		                    138				//Provide password for Login
#define SET_REAL_TIME_CLOCK		                    3		//Set real time clock
#define SET_ADV_PERIOD								8		//Set advertising period
#define SET_GLOBAL_TX_POW							27		//	Sets Tx adv power of device globally
#define SET_ENERGY_SAVE_TIME						29
#define SET_SENSOR_THRESHOLD						9		//Set sensor threshold
#define SET_INSIGMA_FRAME_POWER_SAVING_PARA			40		// Command to Set Insigma frame Advertising and Tx power para
#define PUT_DEVICE_IN_OTA_DFU_MODE					142				//Put the device in direct firmware upgrade mode via over the air
#define RESTART_DEVICE								131				//Restart the device
#define DEEP_SLEEP_CONTROL							17		//Set Deep Sleep 

#define READ_FIRMWARE_DETAIL						129				//Get firmware Detail
#define READ_DIAGNOSTIC_DATA						160
#define READ_ENERGY_SAVING_PARA						161
#define READ_BLE_MAC_ADDR							172
#define READ_INSIGMA_FRAME_POWER_SAVING_PARA		173
#define READ_SENSOR_THRESHOLD					    139	
#define READ_GLOBAL_TX_POW							158
#define READ_CURRENT_TIME							130	
#define READ_ASSOCIATION_PARA						169

#define GLOBAL_TX_POW_CONFIG						1
#define SET_STRT_END_TIME							1
#define GLOBAL_ENERGY_SAVE_CONFIG1					1

//---------------------------------------------------------
/* Use standard Bluetooth HCI error codes for termination reasons:
 * - BT_HCI_ERR_REMOTE_USER_TERM_CONN (0x13): Most common, standard user termination.
 * - BT_HCI_ERR_CONN_TERM_LOCAL_HOST  (0x16): Terminated locally by your microcontroller.
 */
#define DISCONNECT_REASON_STANDARD  BT_HCI_ERR_REMOTE_USER_TERM_CONN

/* -------------------------------------------------------------------- */
/* 1. 128-bit UUID Definitions                                          */
/* -------------------------------------------------------------------- */
/* Service UUID:              00000001-edd2-47b8-b193-bc4e6e6a17b0 */
/* Write Characteristic:      00000002-edd2-47b8-b193-bc4e6e6a17b0 */
/* Read/Notify Characteristic: 00000003-edd2-47b8-b193-bc4e6e6a17b0 */

#define BT_UUID_CUSTOM_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x81469E83, 0xFE6F, 0x42AF, 0xB1C6, 0xF339DBDCE2EA)

#define BT_UUID_CUSTOM_WRITE_CHAR_VAL \
    BT_UUID_128_ENCODE(0x8146C203, 0xFE6F, 0x42AF, 0xB1C6, 0xF339DBDCE2EA)

#define BT_UUID_CUSTOM_NOTIFY_CHAR_VAL \
    BT_UUID_128_ENCODE(0x8146C201, 0xFE6F, 0x42AF, 0xB1C6, 0xF339DBDCE2EA)

#define BT_UUID_CUSTOM_SERVICE     BT_UUID_DECLARE_128(BT_UUID_CUSTOM_SERVICE_VAL)
#define BT_UUID_CUSTOM_WRITE_CHAR  BT_UUID_DECLARE_128(BT_UUID_CUSTOM_WRITE_CHAR_VAL)
#define BT_UUID_CUSTOM_NOTIFY_CHAR BT_UUID_DECLARE_128(BT_UUID_CUSTOM_NOTIFY_CHAR_VAL)

/* -------------------------------------------------------------------- */
/* 2. Public API Functions                                              */
/* -------------------------------------------------------------------- */

/* -------------------------------------------------------------------- */
/* Public API Export Prototypes                                         */
/* -------------------------------------------------------------------- */

/**
 * @brief Deploys the service attribute trees and initializes background timers.
 * @return 0 on successful compilation mapping, negative errno on failure.
 */
int ble_custom_service_init(void);

/**
 * @brief Dispatches an asynchronous GATT notification vector back to a client.
 * @param conn Target active reference connection instance handle.
 * @param data Array source pointer representing byte stream payload payload.
 * @param len  Length constraint boundary.
 * @return 0 on successful air queuing, negative errno on failure.
 */
int ble_custom_service_send_notification(struct bt_conn *conn, const uint8_t *data, uint16_t len);

/**
 * @brief Safely pulls data written by a remote client from internal caches.
 * @param out_data Destination RAM buffer tracking target array slot.
 * @param out_len  Pointer returning length constraint payload metrics.
 * @return 0 if successful, -ENODATA if empty cache layer.
 */
int ble_custom_service_get_written_data(uint8_t *out_data, uint16_t *out_len);

/**
 * @brief Requests the link layer to cleanly isolate and drop the client session.
 * @return 0 on success, negative error sequence on execution stalls.
 */
int ble_custom_service_initiate_disconnect(void);

/**
 * @brief Verifies whether the connected smartphone client has opted in via CCCD.
 */
bool is_char2_notification_enabled(struct bt_conn *conn);

#ifdef __cplusplus
}
#endif

#endif /* BLE_CUSTOM_SERVICE_H_ */