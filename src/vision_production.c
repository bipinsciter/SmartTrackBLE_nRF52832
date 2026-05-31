/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_vision_production.h"
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_uart, LOG_LEVEL_INF);

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

static uint8_t rx_buf_1[UART_RX_BUF_SIZE];
static uint8_t rx_buf_2[UART_RX_BUF_SIZE];

/* Operational state tracking flags */
static bool uart_is_powered = false;
static bool rx_swap_toggle = false;

K_MUTEX_DEFINE(uart_pm_mutex);

/* -------------------------------------------------------------------- */
/* Asynchronous Driver Callback Routine                                 */
/* -------------------------------------------------------------------- */
static void uart_async_callback(const struct device *dev, 
                                struct uart_event *evt, 
                                void *user_data)
{
    switch (evt->type) {
    case UART_TX_DONE:
        LOG_DBG("Async Tx Complete");
        break;

    case UART_RX_RDY:
        if (!uart_is_powered) {
            break; // Drop any lingering frames if suspension is active
        }
        const uint8_t *incoming_ptr = evt->data.rx.buf + evt->data.rx.offset;
        size_t chunk_len = evt->data.rx.len;

        LOG_INF("Inbound Data Received over UART0 (%d bytes)", chunk_len);
        for (size_t i = 0; i < chunk_len; i++) {
            /* Process your application-specific incoming frames here */
            LOG_DBG("Byte = 0x%02x", incoming_ptr[i]);
        }
        break;

    case UART_RX_BUF_REQUEST:
        /* Dynamically ping-pong buffers back to the driver engine */
        if (rx_swap_toggle) {
            uart_rx_buf_rsp(dev, rx_buf_1, UART_RX_BUF_SIZE);
        } else {
            uart_rx_buf_rsp(dev, rx_buf_2, UART_RX_BUF_SIZE);
        }
        rx_swap_toggle = !rx_swap_toggle;
        break;

    case UART_RX_BUF_RELEASED:
    case UART_TX_ABORTED:
        break;

    case UART_RX_DISABLED:
        LOG_INF("UART RX Channel state tracking reported: SHUTDOWN");
        break;

    case UART_RX_STOPPED:
        break;
    }
}

/* -------------------------------------------------------------------- */
/* Power-Managed API Implementations                                    */
/* -------------------------------------------------------------------- */

int app_uart_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART0 device hardware driver is not ready.");
        return -ENODEV;
    }

    int ret = uart_callback_set(uart_dev, uart_async_callback, NULL);
    if (ret != 0) {
        LOG_ERR("Failed to mount async callbacks (%d)", ret);
        return ret;
    }

    /* Keep peripheral off at startup; let the system explicitly choose when to wake it */
    ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
    uart_is_powered = false;

    return ret;
}

int app_uart_enable(void)
{
    int ret = 0;
    k_mutex_lock(&uart_pm_mutex, K_FOREVER);

    if (uart_is_powered) {
        k_mutex_unlock(&uart_pm_mutex);
        return 0; // Already awake
    }

    LOG_INF("Powering up UART0 peripheral hardware clocks...");
    
    /* 1. Request kernel to wake up peripheral hardware */
    ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("Failed to resume power to UART0 peripheral node (%d)", ret);
        k_mutex_unlock(&uart_pm_mutex);
        return ret;
    }

    /* 2. Rearm background DMA channel stream */
    rx_swap_toggle = false;
    ret = uart_rx_enable(uart_dev, rx_buf_1, UART_RX_BUF_SIZE, 5000); // 5ms framing timeout
    if (ret != 0) {
        LOG_ERR("Failed to start DMA tracking streams (%d)", ret);
        pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
        k_mutex_unlock(&uart_pm_mutex);
        return ret;
    }

    uart_is_powered = true;
    LOG_INF("UART0 is awake and listening.");
    
    k_mutex_unlock(&uart_pm_mutex);
    return 0;
}

int app_uart_disable(void)
{
    int ret = 0;
    k_mutex_lock(&uart_pm_mutex, K_FOREVER);

    if (!uart_is_powered) {
        k_mutex_unlock(&uart_pm_mutex);
        return 0; // Already off
    }

    LOG_INF("Deactivating UART0 to save system power...");

    /* 1. Stop the asynchronous DMA engine cleanly first.
     * This triggers the internal UART_RX_DISABLED state handler. */
    ret = uart_rx_disable(uart_dev);
    if (ret != 0 && ret != -EALREADY) {
        LOG_WRN("Warning during RX pipeline flush sequence (%d)", ret);
    }

    /* Give the hardware pipeline a small window to flush remaining ticks */
    k_sleep(K_MSEC(10));

    /* 2. Force the driver hardware blocks to sleep completely */
    ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
    if (ret != 0) {
        LOG_ERR("Failed to cleanly place UART0 block into suspend mode (%d)", ret);
    } else {
        uart_is_powered = false;
        LOG_INF("UART0 hardware suspended successfully. Power rails isolated.");
    }

    k_mutex_unlock(&uart_pm_mutex);
    return ret;
}

int app_uart_transmit(const uint8_t *data, size_t len)
{
    int ret;
    k_mutex_lock(&uart_pm_mutex, K_FOREVER);

    if (!uart_is_powered) {
        LOG_WRN("Transmission rejected: UART0 peripheral is currently suspended.");
        k_mutex_unlock(&uart_pm_mutex);
        return -EACCES;
    }

    ret = uart_tx(uart_dev, data, len, SYS_FOREVER_US);
    
    k_mutex_unlock(&uart_pm_mutex);
    return ret;
}