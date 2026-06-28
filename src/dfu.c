/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_version.h>

#include <zephyr/kernel.h>

#include <zephyr/bluetooth/uuid.h>

#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>

#include "app_dfu.h"
#include "app_factory_reset.h"
#include "app_ui.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(fp_fmdn, LOG_LEVEL_INF);

/* DFU mode timeout in minutes. */
#define DFU_MODE_TIMEOUT_MIN (2)

static bool dfu_mode;

static void dfu_mode_timeout_work_handle(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(dfu_mode_timeout_work, dfu_mode_timeout_work_handle);

BUILD_ASSERT((APP_VERSION_MAJOR == CONFIG_BT_FAST_PAIR_FMDN_DULT_FIRMWARE_VERSION_MAJOR) &&
	     (APP_VERSION_MINOR == CONFIG_BT_FAST_PAIR_FMDN_DULT_FIRMWARE_VERSION_MINOR) &&
	     (APP_PATCHLEVEL == CONFIG_BT_FAST_PAIR_FMDN_DULT_FIRMWARE_VERSION_REVISION),
	     "Firmware version mismatch. Update the DULT FW version in the Kconfig file "
	     "to be aligned with the VERSION file.");

static void dfu_mode_change(bool new_mode)
{
	if (dfu_mode == new_mode) {
		return;
	}

	LOG_INF("DFU: mode %sabled", new_mode ? "en" : "dis");

	dfu_mode = new_mode;

	app_ui_state_change_indicate(APP_UI_STATE_DFU_MODE, dfu_mode);
}

static void dfu_factory_reset_prepare(void)
{
	dfu_mode_change(false);
}

APP_FACTORY_RESET_CALLBACKS_REGISTER(factory_reset_cbs, dfu_factory_reset_prepare, NULL);

bool app_dfu_bt_gatt_operation_allow(const struct bt_uuid *uuid)
{
	if (bt_uuid_cmp(uuid, SMP_BT_CHR_UUID) != 0) {
		return true;
	}

	if (!dfu_mode) {
		LOG_WRN("DFU: SMP characteristic access denied, DFU mode is not active");
		return false;
	}

	(void) k_work_reschedule(&dfu_mode_timeout_work, K_MINUTES(DFU_MODE_TIMEOUT_MIN));

	return true;
}


static void dfu_mode_action_handle(void)
{
	if (dfu_mode) {
		LOG_INF("DFU: refreshing the DFU mode timeout");
	} else {
		LOG_INF("DFU: entering the DFU mode for %d minute(s)",
			DFU_MODE_TIMEOUT_MIN);
	}

	(void) k_work_reschedule(&dfu_mode_timeout_work, K_MINUTES(DFU_MODE_TIMEOUT_MIN));

	dfu_mode_change(true);
}

void app_dfu_enter_mode_custom(void)
{
    /* Hand it over smoothly to the cooperative thread execution context */
    dfu_mode_action_handle();
}

static void dfu_mode_timeout_work_handle(struct k_work *w)
{
	LOG_INF("DFU: timeout expired");

	dfu_mode_change(false);
}

void app_dfu_fw_version_log(void)
{
	LOG_INF("DFU: Firmware version: %s", APP_VERSION_TWEAK_STRING);
}

static void dfu_mode_request_handle(enum app_ui_request request)
{
	/* It is assumed that the callback executes in the cooperative
	 * thread context as it interacts with the FMDN API.
	 */
	__ASSERT_NO_MSG(!k_is_preempt_thread());
	__ASSERT_NO_MSG(!k_is_in_isr());

	if (request == APP_UI_REQUEST_DFU_MODE_ENTER) {
		dfu_mode_action_handle();
	}
}

APP_UI_REQUEST_LISTENER_REGISTER(dfu_mode_request_handler, dfu_mode_request_handle);
