/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/devicetree.h>

#include <bluetooth/services/fast_pair/fmdn.h>

#include "app_motion_detector.h"
#include "app_ui.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(motion, LOG_LEVEL_INF);

static const struct gpio_dt_spec lis3dh_int = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), signal_gpios);

static bool motion_detector_active;
static bool motion_detected;

static void fmdn_motion_detector_start(void)
{
	__ASSERT_NO_MSG(!motion_detector_active);
	LOG_INF("FMDN: motion detector started");
	motion_detector_active = true;

	app_ui_state_change_indicate(APP_UI_STATE_MOTION_DETECTOR_ACTIVE, motion_detector_active);
}

static bool fmdn_motion_detector_period_expired(void)
{
	motion_detected = gpio_pin_get_dt(&lis3dh_int);

	__ASSERT_NO_MSG(motion_detector_active);
	LOG_INF("FMDN: motion detector period expired. Reporting that the motion was %sdetected",
		motion_detected ? "" : "not ");

	return motion_detected;
}

static void fmdn_motion_detector_stop(void)
{
	__ASSERT_NO_MSG(motion_detector_active);
	LOG_INF("FMDN: motion detector stopped");
	motion_detector_active = false;
	
	app_ui_state_change_indicate(APP_UI_STATE_MOTION_DETECTOR_ACTIVE, motion_detector_active);
	motion_detected = false;
}

static const struct bt_fast_pair_fmdn_motion_detector_cb fmdn_motion_detector_cb = {
	.start = fmdn_motion_detector_start,
	.period_expired = fmdn_motion_detector_period_expired,
	.stop = fmdn_motion_detector_stop,
};

int app_motion_detector_init(void)
{
	int ret;

	ret = bt_fast_pair_fmdn_motion_detector_cb_register(&fmdn_motion_detector_cb);
	if (ret) {
		LOG_ERR("FMDN: bt_fast_pair_fmdn_motion_detector_cb_register failed (err %d)", ret);
		return ret;
	}

	return 0;
}
