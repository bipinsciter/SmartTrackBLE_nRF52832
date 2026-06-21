/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/devicetree.h>

#include "app_vision_production.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(lis3dh, LOG_LEVEL_INF);

/* LIS3DH I2C address (SA0 = 0 => 0x18, SA0 = 1 => 0x19) */
#define LIS3DH_I2C_ADDR 0x19

/* LIS3DH registers */
#define LIS3DH_WHO_AM_I      0x0F
#define LIS3DH_CTRL_REG1     0x20
#define LIS3DH_CTRL_REG2     0x21
#define LIS3DH_CTRL_REG3     0x22
#define LIS3DH_CTRL_REG4     0x23
#define LIS3DH_CTRL_REG5     0x24
#define LIS3DH_CTRL_REG6     0x25
#define LIS3DH_INT1_CFG      0x30
#define LIS3DH_INT1_THS      0x32
#define LIS3DH_INT1_DURATION 0x33

/* Example register values (tune these) */
#define REG_CTRL_REG1_VAL 0x17  /* ODR = 1Hz, XYZ enabled */
#define REG_CTRL_REG1_VAL_PD 0x08  /* Low power mode, XYZ Disable */
#define REG_CTRL_REG2_VAL 0x09  /* higpass filter enabled */
#define REG_CTRL_REG3_VAL 0x40  /* INT1 on IA (push-pull, active high) */
#define REG_CTRL_REG3_VAL_PD 0x00  /* INT1 disable */
#define REG_CTRL_REG4_VAL 0x00
#define REG_CTRL_REG5_VAL 0x00
#define REG_CTRL_REG6_VAL 0x00
#define REG_INT1_CFG_VAL  0x15  /* movement on X/Y/Z with OR */
#define REG_INT1_THS_VAL  0x05//0x19  /* threshold - tune (LSB ~ 16 mg at FS=2g) */
#define REG_INT1_DUR_VAL  0x0A  /* duration in number of samples */

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static const struct gpio_dt_spec lis3dh_int = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), signal_gpios);
static struct gpio_callback lis3dh_int_cb_data;

static int i2c_write_reg(uint8_t reg, uint8_t val)
{
    return i2c_reg_write_byte(i2c_dev, LIS3DH_I2C_ADDR, reg, val);
}

static int i2c_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_reg_read_byte(i2c_dev, LIS3DH_I2C_ADDR, reg, val);
}

void lis3dh_int_callback(const struct device *port, 
                               struct gpio_callback *cb, 
                               gpio_port_pins_t pins)
{
    /* Read the immediate state of the pin to determine which edge occurred */
    int pin_state = gpio_pin_get_dt(&lis3dh_int);

    if (pin_state > 0) {
        LOG_INF("LIS3DH Motion Interrupt Restored!");
    } else {
        LOG_INF("LIS3DH Motion Interrupt Triggered!");
    }
}

/* Setup LIS3DH for wake-on-motion interrupt on INT1 */
int lis3dh_setup(uint8_t Thresold, uint8_t Duration)
{
    int ret;
    uint8_t who = 0;

    // Give the LIS3DH time to fully boot its internal digital structures
    k_msleep(500);
    
    LOG_INF("LIS3DH Initialization");

    ret = i2c_read_reg(LIS3DH_WHO_AM_I, &who);
    if (ret) {
        LOG_ERR("Failed to read WHO_AM_I (%d)", ret);
        return ret;
    }
    LOG_INF("LIS3DH WHO_AM_I = 0x%02x", who);
    if (who != 0x33) {
        LOG_WRN("Unexpected WHO_AM_I value - check wiring/address");
        /* still continue; some clones may differ */
    }

    /* CTRL_REG1: ODR = 1Hz (0x10), enable X/Y/Z (0x07) => 0x17 */
    ret = i2c_write_reg(LIS3DH_CTRL_REG1, REG_CTRL_REG1_VAL);
    if (ret) return ret;

	/* CTRL_REG2:  higpass filter enabled */
    ret = i2c_write_reg(LIS3DH_CTRL_REG2, REG_CTRL_REG2_VAL);
    if (ret) return ret;

    /* CTRL_REG3: Route IA1 on INT1 pin */
    ret = i2c_write_reg(LIS3DH_CTRL_REG3, REG_CTRL_REG3_VAL);
    if (ret) return ret;

	/* CTRL_REG4: */
    ret = i2c_write_reg(LIS3DH_CTRL_REG4, REG_CTRL_REG4_VAL);
    if (ret) return ret;

	/* CTRL_REG5: */
    ret = i2c_write_reg(LIS3DH_CTRL_REG5, REG_CTRL_REG5_VAL);
    if (ret) return ret;

	/* CTRL_REG6: */
    ret = i2c_write_reg(LIS3DH_CTRL_REG6, REG_CTRL_REG6_VAL);
    if (ret) return ret;

    /* INT1_CFG: high event configuration for X/Y/Z */
    ret = i2c_write_reg(LIS3DH_INT1_CFG, REG_INT1_CFG_VAL);
    if (ret) return ret;

    /* INT1_THS: threshold (1 LSB ~16 mg at FS=2g), tune as needed */
    ret = i2c_write_reg(LIS3DH_INT1_THS, Thresold);
    if (ret) return ret;

    /* INT1_DURATION: duration (in ODR samples) to validate event */
    ret = i2c_write_reg(LIS3DH_INT1_DURATION, Duration);
    if (ret) return ret;

    LOG_INF("LIS3DH INT Configure");

    /* 1. Verify that the hardware device instance is ready for use */
	if (!gpio_is_ready_dt(&lis3dh_int)) {
		LOG_ERR("Error: button device %s is not ready\n",
		       lis3dh_int.port->name);
		return -ENODEV;
	}

    /* 2. Configure the physical pin direction based on DTS specifications */
	ret = gpio_pin_configure_dt(&lis3dh_int, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       ret, lis3dh_int.port->name, lis3dh_int.pin);
		return ret;
	}

    ret = gpio_pin_interrupt_configure_dt(&lis3dh_int, GPIO_INT_EDGE_BOTH);
    if (ret != 0) {
        LOG_ERR("Error configuring interrupt trigger rules (%d)", ret);
        return ret;
    }

    /* 4. Link the callback structure to your custom handling function */
    gpio_init_callback(&lis3dh_int_cb_data, lis3dh_int_callback, BIT(lis3dh_int.pin));

    /* 5. Attach the callback hook directly into the active GPIO driver port */
    ret = gpio_add_callback(lis3dh_int.port, &lis3dh_int_cb_data);
    if (ret != 0) {
        LOG_ERR("Error mounting software callback routing pointer (%d)", ret);
        return ret;
    }

    ret = pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_SUSPEND);
    if (ret != 0) {
        LOG_ERR("I2C0 suspended failed (%d)", ret);
    } else {
        LOG_INF("I2C0 suspended successfully");
    }

    LOG_INF("LIS3DH configured for wake-on-motion (INT1)");
    return 0;
}

/* Power down LIS3DH */
int lis3dh_update(uint8_t Thresold, uint8_t Duration)
{
    int ret;

    ret = pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_RESUME);
    if (ret != 0) {
        LOG_ERR("I2C0 resumed failed (%d)", ret);
    } else {
        LOG_INF("I2C0 resumed successfully");
    }

    /* INT1_THS: threshold (1 LSB ~16 mg at FS=2g), tune as needed */
    ret = i2c_write_reg(LIS3DH_INT1_THS, Thresold);
    if (ret) return ret;

    /* INT1_DURATION: duration (in ODR samples) to validate event */
    ret = i2c_write_reg(LIS3DH_INT1_DURATION, Duration);
    if (ret) return ret;

    ret = pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_SUSPEND);
    if (ret != 0) {
        LOG_ERR("I2C0 suspended failed (%d)", ret);
    } else {
        LOG_INF("I2C0 suspended successfully");
    }
    
    return 0;
}

/* Power down LIS3DH */
int lis3dh_powerdown(void)
{
    int ret;
    uint8_t who = 0;

    ret = i2c_read_reg(LIS3DH_WHO_AM_I, &who);
    if (ret) {
        LOG_ERR("Failed to read WHO_AM_I (%d)", ret);
        return ret;
    }

    LOG_INF("LIS3DH WHO_AM_I = 0x%02x", who);

    if (who != 0x33) {
        LOG_WRN("Unexpected WHO_AM_I value - check wiring/address");
        /* still continue; some clones may differ */
    }
    else
    {
        /* CTRL_REG1: Low Power mode, Disable X/Y/Z (0x08) => 0x08 */
        ret = i2c_write_reg(LIS3DH_CTRL_REG1, REG_CTRL_REG1_VAL_PD);
        if (ret) return ret;

        /* CTRL_REG3: Disable INT1 pin */
        ret = i2c_write_reg(LIS3DH_CTRL_REG3, REG_CTRL_REG3_VAL_PD);
        if (ret) return ret;

        LOG_INF("Put LIS3DH in power down mode");
    }
    
    return 0;
}
