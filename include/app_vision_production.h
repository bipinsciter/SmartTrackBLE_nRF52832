/*
 * Copyright (c) 2026 Vision Consultancy
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UART_H_
#define APP_UART_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UART_RX_BUF_SIZE    64

/**
 * @brief Performs base configuration of the UART0 peripheral node.
 * Does NOT activate the radio power or RX streams automatically.
 * @return int 0 on success, negative error code on failure.
 */
int app_uart_init(void);

/**
 * @brief Powers up the UART0 peripheral hardware and activates the DMA RX stream.
 * @return int 0 on success, negative error code on failure.
 */
int app_uart_enable(void);

/**
 * @brief Stops active RX streams and cleanly suspends the UART0 peripheral 
 * to save power.
 * @return int 0 on success, negative error code on failure.
 */
int app_uart_disable(void);

/**
 * @brief Transmits data over UART0. Fails automatically if the peripheral is disabled.
 * @param data Pointer to the source byte array.
 * @param len  Number of bytes to transmit.
 * @return int 0 on success, negative error code on failure.
 */
int app_uart_transmit(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_UART_H_ */