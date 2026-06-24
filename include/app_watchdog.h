#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

/**
 * @brief Initializes the hardware watchdog peripheral and sets up the timeout window.
 * @return 0 on success, negative error code on failure.
 */
int app_watchdog_init(void);

/**
 * @brief Starts the background worker loop that periodically feeds the watchdog.
 */
void app_watchdog_start_feeding(void);

#endif /* APP_WATCHDOG_H */
