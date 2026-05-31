#ifndef NVS_CONFIG_STORAGE_H_
#define NVS_CONFIG_STORAGE_H_

#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------- */
/* 1. Configuration Structures Definitions                               */
/* -------------------------------------------------------------------- */

/**
 * @brief Fixed Configuration Structure (512 Bytes)
 * Stored in the dedicated 'fixData_storage' NVS partition.
 * Typically populated during manufacturing or first boot.
 */
struct fix_config_t {
    uint32_t magic_number;
    uint8_t hardware_version;
    uint8_t production_id[16];
    uint8_t encryption_key[32];
    uint8_t reserved[459]; // Padding to maintain structural alignment/footprint
};

/**
 * @brief Dynamic Configuration Structure
 * Stored in the dedicated 'DynaData_storage' NVS partition.
 * Frequently updated during standard runtime operation.
 */
struct dyn_config_t {
    uint32_t boot_count;
    uint16_t last_calibrated_value;
    uint8_t device_mode;
    uint8_t ble_mac_override[6];
};

/* -------------------------------------------------------------------- */
/* 2. Public API Functions                                              */
/* -------------------------------------------------------------------- */

/**
 * @brief Main power-up initialization function.
 * * This function mounts both NVS storage partitions, reads existing profiles 
 * from non-volatile storage into active RAM instances, and establishes factory 
 * fallback defaults if empty (first boot). Call this early in main().
 */
void storage_load_on_powerup(void);

/**
 * @brief Commits updated Fixed Configuration data into NVS flash.
 * * @param cfg Pointer to the source fixed config structure to save.
 * @return 0 on success, negative error code from the underlying driver on failure.
 */
int storage_write_fix_config(const struct fix_config_t *cfg);

/**
 * @brief Commits updated Dynamic Configuration data into NVS flash.
 * * @param cfg Pointer to the source dynamic config structure to save.
 * @return 0 on success, negative error code from the underlying driver on failure.
 */
int storage_write_dyn_config(const struct dyn_config_t *cfg);

/**
 * @brief Retrieves a copy of the current in-RAM Fixed Configuration data.
 * * @param dest Destination pointer where data will be copied.
 */
void get_active_fix_config(struct fix_config_t *dest);

/**
 * @brief Retrieves a copy of the current in-RAM Dynamic Configuration data.
 * * @param dest Destination pointer where data will be copied.
 */
void get_active_dyn_config(struct dyn_config_t *dest);

#ifdef __cplusplus
}
#endif

#endif /* NVS_CONFIG_STORAGE_H_ */