#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <zephyr/drivers/flash.h>

LOG_MODULE_REGISTER(unified_storage, LOG_LEVEL_INF);

/* -------------------------------------------------------------------- */
/* 1. Structures and Definitions                                        */
/* -------------------------------------------------------------------- */

struct fix_config_t {
    uint32_t magic_number;
    uint8_t hardware_version;
    uint8_t production_id[16];
    uint8_t encryption_key[32];
};

struct dyn_config_t {
    uint32_t boot_count;
    uint16_t last_calibrated_value;
    uint8_t device_mode;
    uint8_t ble_mac_override[6];
};

// Global active runtime configurations
static struct fix_config_t current_fix_config;
static struct dyn_config_t current_dyn_config;

// Single NVS File System instance
static struct nvs_fs fs_shared;

// Distinct NVS IDs within the same file system partition
#define NVS_ID_FIX_CONFIG 1
#define NVS_ID_DYN_CONFIG 2

/* -------------------------------------------------------------------- */
/* 2. NVS Initialization (Using the 8KB Partition)                      */
/* -------------------------------------------------------------------- */

int init_unified_storage(void)
{
    int rc;
    struct flash_pages_info info;
    const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

    if (!device_is_ready(flash_dev)) {
        LOG_ERR("Flash device not ready");
        return -ENODEV;
    }

    // Bind to the unified partition (e.g., fixData_storage, now sized to 8KB)
    fs_shared.flash_device = flash_dev;
    fs_shared.offset = FIXED_PARTITION_OFFSET(appconfig_storage);
    
    rc = flash_get_page_info_by_offs(fs_shared.flash_device, fs_shared.offset, &info);
    if (rc) {
        LOG_ERR("Failed to get page info (%d)", rc);
        return rc;
    }
    
    fs_shared.sector_size = info.size;
    fs_shared.sector_count = 2; // 8KB partition / 4KB sector size = 2 sectors (Min required)

    rc = nvs_mount(&fs_shared);
    if (rc) {
        LOG_ERR("Failed to mount unified NVS partition (%d)", rc);
        return rc;
    }

    LOG_INF("Unified NVS partition mounted successfully.");
    return 0;
}

/* -------------------------------------------------------------------- */
/* 3. Separate Runtime Write APIs                                       */
/* -------------------------------------------------------------------- */

int storage_write_fix_config(const struct fix_config_t *cfg)
{
    if (cfg == NULL) return -EINVAL;
    
    // Writes to ID 1
    int rc = nvs_write(&fs_shared, NVS_ID_FIX_CONFIG, cfg, sizeof(struct fix_config_t));
    if (rc >= 0) {
        memcpy(&current_fix_config, cfg, sizeof(struct fix_config_t));
        LOG_INF("Fixed config written to ID %d", NVS_ID_FIX_CONFIG);
        return 0;
    }
    LOG_ERR("Failed to write fixed config (%d)", rc);
    return rc;
}

int storage_write_dyn_config(const struct dyn_config_t *cfg)
{
    if (cfg == NULL) return -EINVAL;

    // Writes to ID 2
    int rc = nvs_write(&fs_shared, NVS_ID_DYN_CONFIG, cfg, sizeof(struct dyn_config_t));
    if (rc >= 0) {
        memcpy(&current_dyn_config, cfg, sizeof(struct dyn_config_t));
        LOG_INF("Dynamic config written to ID %d", NVS_ID_DYN_CONFIG);
        return 0;
    }
    LOG_ERR("Failed to write dynamic config (%d)", rc);
    return rc;
}

/* -------------------------------------------------------------------- */
/* 4. Unified Power-up Read Logic                                       */
/* -------------------------------------------------------------------- */

void storage_load_on_powerup(void)
{
    int rc;

    if (init_unified_storage() != 0) {
        LOG_ERR("Could not initialize storage filesystem!");
        return;
    }

    /* --- Load Fixed Configuration (ID 1) --- */
    rc = nvs_read(&fs_shared, NVS_ID_FIX_CONFIG, &current_fix_config, sizeof(struct fix_config_t));
    if (rc <= 0) {
        LOG_WRN("No Fixed Config found (ID 1). Initializing defaults...");
        current_fix_config.magic_number = 0x54414758; // "TAGX"
        current_fix_config.hardware_version = 1;
        memset(current_fix_config.production_id, 0xAA, 16);
        
        storage_write_fix_config(&current_fix_config);
    } else {
        LOG_INF("Loaded Fixed Config (ID 1). Magic: 0x%08X", current_fix_config.magic_number);
    }

    /* --- Load Dynamic Configuration (ID 2) --- */
    rc = nvs_read(&fs_shared, NVS_ID_DYN_CONFIG, &current_dyn_config, sizeof(struct dyn_config_t));
    if (rc <= 0) {
        LOG_WRN("No Dynamic Config found (ID 2). Initializing defaults...");
        current_dyn_config.boot_count = 1;
        current_dyn_config.device_mode = 0;
        current_dyn_config.last_calibrated_value = 0;
        
        storage_write_dyn_config(&current_dyn_config);
    } else {
        current_dyn_config.boot_count++;
        storage_write_dyn_config(&current_dyn_config);
        LOG_INF("Loaded Dynamic Config (ID 2). Boot count: %d", current_dyn_config.boot_count);
    }
}

/* -------------------------------------------------------------------- */
/* 5. Getters for Application Context                                   */
/* -------------------------------------------------------------------- */

void get_active_fix_config(struct fix_config_t *dest) {
    memcpy(dest, &current_fix_config, sizeof(struct fix_config_t));
}

void get_active_dyn_config(struct dyn_config_t *dest) {
    memcpy(dest, &current_dyn_config, sizeof(struct dyn_config_t));
}