/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#include "app_watchdog.h"

LOG_MODULE_REGISTER(app_wdt, LOG_LEVEL_INF);

/* Automatically fetch the default watchdog spec from the devicetree (wdt0) */
static const struct device *const wdt_dev = DEVICE_DT_GET(DT_NODELABEL(wdt0));

static int wdt_channel_id;
static struct k_work_delayable wdt_feed_work;

/* -------------------------------------------------------------------- */
/* Watchdog Feeding Execution Loop                                      */
/* -------------------------------------------------------------------- */

static void wdt_feed_work_handler(struct k_work *work)
{
    /* 1. Feed the hardware peripheral channel to reset the internal countdown clock */
    wdt_feed(wdt_dev, wdt_channel_id);
    LOG_DBG("Hardware Watchdog Kicked!");

    /* 2. Schedule the next feed event. 
     * RULE: Feed at exactly HALF the timeout window to handle thread latencies safely.
     * Since our timeout window is 8 seconds, we kick it every 4 seconds. */
    k_work_schedule(&wdt_feed_work, K_SECONDS(4));
}

/* -------------------------------------------------------------------- */
/* Public API Implementations                                           */
/* -------------------------------------------------------------------- */

int app_watchdog_init(void)
{
    struct wdt_timeout_cfg wdt_config = {0};

    if (!device_is_ready(wdt_dev)) {
        LOG_ERR("Watchdog hardware peripheral device block not ready!");
        return -ENODEV;
    }

    /* Configure Watchdog Behavior Parameters */
    wdt_config.flags = WDT_FLAG_RESET_SOC; /* Hard reset the chip if the timer hits 0 */
    wdt_config.window.min = 0U;            /* No minimum window constraint */
    wdt_config.window.max = 8000U;         /* 8-second execution safety window (8000 ms) */
    wdt_config.callback = NULL;            /* Not used if resetting the whole SoC */

    /* Install the configuration channel */
    wdt_channel_id = wdt_install_timeout(wdt_dev, &wdt_config);
    if (wdt_channel_id < 0) {
        LOG_ERR("Watchdog timeout configuration installation failed (err %d)", wdt_channel_id);
        return wdt_channel_id;
    }

    /* Start the hardware clock countdown engine */
    int err = wdt_setup(wdt_dev, WDT_OPT_PAUSE_IN_SLEEP);
    if (err) {
        LOG_ERR("Failed to enable hardware watchdog controller (err %d)", err);
        return err;
    }

    /* Initialize the delayable feeding work structure */
    k_work_init_delayable(&wdt_feed_work, wdt_feed_work_handler);

    LOG_INF("Hardware Watchdog successfully armed. Window limit: 8 seconds.");
    return 0;
}

void app_watchdog_start_feeding(void)
{
    /* Fire off the first background execution kick immediately */
    k_work_schedule(&wdt_feed_work, K_NO_WAIT);
}
