#ifndef BLE_ADV_CUSTOM_H_
#define BLE_ADV_CUSTOM_H_

#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------- */
/* 1. Structures for Custom Dynamic Data                                */
/* -------------------------------------------------------------------- */

/**
 * @brief Structure matching the layout for your Service Data (0x16)
 * Contains a 16-bit Service UUID followed by custom payload bytes.
 */
struct ble_service_data_t {
    uint16_t service_uuid;    // 2 Bytes (e.g., 0x180F for Battery Service)
    uint8_t payload[7];       // 7 Bytes custom operational status/sensor metrics
};

/**
 * @brief Structure matching the layout for your Manufacturer Data (0xFF)
 * Contains a Company Identifier followed by custom corporate payload bytes.
 */
struct ble_mfg_data_t {
    uint16_t company_id;      // 2 Bytes Company ID (e.g., 0x0059 for Nordic Semiconductor)
    uint8_t payload[6];       // 6 Bytes MAC
};

/* -------------------------------------------------------------------- */
/* 2. Public API Functions                                              */
/* -------------------------------------------------------------------- */

/**
 * @brief Initializes the underlying Bluetooth stack subsystem.
 * @return 0 on success, or negative error code on failure.
 */
int ble_adv_custom_init(void);

/**
 * @brief Direct API to begin connectable advertising with the configured structures.
 * @return 0 on success, or negative error code on failure.
 */
int ble_adv_custom_start(void);

/**
 * @brief Halts the active advertiser immediately.
 */
int ble_adv_custom_stop(void);

int getVisionId(void);

/**
 * @brief Updates the active BLE advertisement payload on-the-fly.
 * * @param svc_data Pointer to the updated Service Data structure.
 * @param mfg_data Pointer to the updated Manufacturer Data structure.
 * @return 0 on success, or negative error code on failure.
 */
int ble_adv_custom_update(const struct ble_service_data_t *svc_data, 
                          const struct ble_mfg_data_t *mfg_data);

#ifdef __cplusplus
}
#endif

#endif /* BLE_ADV_CUSTOM_H_ */