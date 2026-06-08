/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

/* Zephyr include files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <string.h>

/* Application include files */
#include "app_nvs_storage.h"

/* Registering Log Module */
LOG_MODULE_REGISTER(sh_nvs_handler, LOG_LEVEL_INF);

/* NVS partition definitions */
#define VISION_NVS_PARTITION        appconfig_storage
#define VISION_NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(VISION_NVS_PARTITION)
#define VISION_NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(VISION_NVS_PARTITION)

#define VISION_NVS_FS_SECTOR_CNT    2U  // Number of sectors in the file system

/* Global structure to handle NVS file system operations */
static struct nvs_fs g_fs; 

int read_nvs_data(uint16_t key_id, void *read_buff, size_t data_len)
{
    // nvs_read returns the number of bytes read on success
    int bytes_read = nvs_read(&g_fs, key_id, read_buff, data_len);
    if (bytes_read < 0) {
        LOG_WRN("NVS read key %d failed or empty (err %d)", key_id, bytes_read);
    }
    return bytes_read;
}

int write_nvs_data(uint16_t key_id, const void *write_buff, size_t data_len)
{
    // nvs_write returns the number of bytes written on success
    int rc = nvs_write(&g_fs, key_id, write_buff, data_len);
    if (rc < 0) {
        LOG_ERR("Failed to write data at key %d with Error %d", key_id, rc);
    } else if (rc == 0) {
        LOG_DBG("Key %d data skipped writing: contents identical to flash cache", key_id);
    }
    return rc;
}

int delete_nvs_data(uint16_t key_id)
{
    int rc = nvs_delete(&g_fs, key_id);
    if (rc < 0) {
        LOG_ERR("Failed to delete data entry at key %d with Error %d", key_id, rc);
    }
    return rc;
}

int app_nvs_handler_init(void) 
{
    struct flash_pages_info info;
    int ret;

    memset(&info, 0, sizeof(info));

    // 1. Assign and verify flash peripheral hardware bindings
    g_fs.flash_device = VISION_NVS_PARTITION_DEVICE;
    if (!device_is_ready(g_fs.flash_device)) {
        LOG_ERR("Flash hardware driver mapping for NVS is not ready");
        return -ENODEV;
    }
    LOG_DBG("NVS physical flash layer ready");

    // 2. Map file system boundaries inside the flash table layout
    g_fs.offset = VISION_NVS_PARTITION_OFFSET;
    
    ret = flash_get_page_info_by_offs(g_fs.flash_device, g_fs.offset, &info);
    if (ret) {
        LOG_ERR("Failed to capture flash geometry parameters (err: %d)", ret);
        return ret;
    }
    
    g_fs.sector_size = info.size;
    g_fs.sector_count = VISION_NVS_FS_SECTOR_CNT;
    
    LOG_INF("NVS Geometry: Sector Size = %d Bytes, Total Count Specified = %d", 
            g_fs.sector_size, g_fs.sector_count);

    LOG_DBG("Attempting to mount flash sector entries...");
    
    // 3. Mount the NVS system. This formats the storage block automatically if empty
    ret = nvs_mount(&g_fs);
    if (ret) {
        LOG_ERR("CRITICAL: File system storage block mount failed (err: %d)", ret);
        return ret;
    }
    
    LOG_INF("Non-Volatile Storage (NVS) file system mounted successfully.");
    return 0;
}
