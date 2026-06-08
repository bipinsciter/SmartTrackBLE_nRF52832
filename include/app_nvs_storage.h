/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NVS_HANDLER_H_
#define NVS_HANDLER_H_

#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ensure these IDs match your key structure configuration */
#define PRODUCTION_DATA_KEY  1
#define CONFIG_DATA_KEY      2
#define DYNAMIC_DATA_KEY     3

/**
 * @brief Initialize the Non-Volatile Storage (NVS) file system.
 * * @return 0 if successful, negative system errno value on failure.
 */
int app_nvs_handler_init(void);

/**
 * @brief Read data from NVS file system.
 * * @param key_id    The unique identification token/ID for the slot.
 * @param read_buff Pointer to the buffer where retrieved data is copied.
 * @param data_len  The expected size of the data to be read.
 * * @return Number of bytes read on success, or a negative errno on failure.
 */
int read_nvs_data(uint16_t key_id, void *read_buff, size_t data_len);

/**
 * @brief Write data to NVS file system.
 * * @param key_id     The unique identification token/ID for the slot.
 * @param write_buff Pointer to the data payload to be stored.
 * @param data_len   Length of the data to write.
 * * @return Number of bytes written on success (0 if data is identical), 
 * or a negative errno on failure.
 */
int write_nvs_data(uint16_t key_id, const void *write_buff, size_t data_len);

/**
 * @brief Delete data slot from NVS file system.
 * * @param key_id The unique identification token/ID to be wiped.
 * * @return 0 if successful, negative system errno value on failure.
 */
int delete_nvs_data(uint16_t key_id);

#ifdef __cplusplus
}
#endif

#endif /* NVS_HANDLER_H_ */