/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_TIME_MANAGER_H_
#define APP_TIME_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Master time setter. Call this when phone app synchronizes time over BLE.
 */
void app_time_sync_set_utc(uint32_t u32_IncomingEpochSeconds);

/**
 * @brief Returns current UTC Unix Epoch.
 */
uint32_t app_time_get_utc_epoch(void);

/**
 * @brief Returns the localized count of minutes elapsed since midnight (0 to 1439).
 */
uint16_t app_time_get_local_minutes_of_day(void);

/**
 * @brief Check if eco low-power states should be applied based on local time parameters.
 */
bool app_time_is_within_energy_saving_window(void);

void app_time_activities_init(void);

#endif /* APP_TIME_MANAGER_H_ */
