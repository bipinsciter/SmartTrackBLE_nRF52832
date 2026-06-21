/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_VISION_MOTION_H_
#define APP_VISION_MOTION_H_

/**
 * @defgroup fmdn_sample_motion_detector Locator Tag sample motion detector module
 * @brief Locator Tag sample motion detector module
 *
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

int lis3dh_setup(uint8_t Thresold, uint8_t Duration);

int lis3dh_powerdown(void);

int lis3dh_update(uint8_t Thresold, uint8_t Duration);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* APP_VISION_MOTION_H_ */
