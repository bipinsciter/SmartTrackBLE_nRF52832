#ifndef BLE_CUSTOM_SERVICE_H_
#define BLE_CUSTOM_SERVICE_H_

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CUSTOM_MAX_DATA_LEN 240

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

/**
 * @brief Initialize the service memory structures.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_custom_service_init(void);

/**
 * @brief Push a notification payload to the central phone if subscriptions are active.
 * This also updates the read buffer cache simultaneously.
 * @param data Pointer to the buffer containing data to transmit.
 * @param len Length of the data payload (Max 240 bytes).
 * @return 0 on success, or a negative error code on failure.
 */
int ble_custom_service_send_notification(struct bt_conn *conn, const uint8_t *data, uint16_t len);

/**
 * @brief Retrieves the latest chunk written by the central client application.
 * @param out_data Pointer to local buffer destination.
 * @param out_len Pointer to track pulled size.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_custom_service_get_written_data(uint8_t *out_data, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* BLE_CUSTOM_SERVICE_H_ */