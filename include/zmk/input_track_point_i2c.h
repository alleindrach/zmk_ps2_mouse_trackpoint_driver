/*
 * Copyright (c) 2016 alleindrach@gmail.com,.
 *
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define MOUSE_I2C_SETTINGS_SUBTREE "mouse_i2c"

#define MOUSE_I2C_CMD_TP_SLEEP "\x22\x00\x01\x08"
#define MOUSE_I2C_CMD_TP_WAKEUP "\x22\x00\x00\x08"
#define MOUSE_I2C_CMD_TP_ENTER_IDLE "\x25\x00\x06\x00\x29\x06\x06\x01"
#define MOUSE_I2C_CMD_TP_EXIT_IDLE "\x25\x00\x06\x00\x29\x06\x06\x00"
#define MOUSE_I2C_CMD_TP_RESET "\x25\x00\x06\x00\x29\x77\x77\x77"


#define MOUSE_I2C_ST_TP_MOVING_SPEED "tp_moving_speed"
#define MOUSE_I2C_CMD_TP_SET_MOVING_SPEED_VERTICAL "\x25\x00\x06\x00\x29\x42"
#define MOUSE_I2C_CMD_TP_SET_MOVING_SPEED_HORIZONTAL "\x25\x00\x06\x00\x29\x43"


#define MOUSE_I2C_MOVING_SPEED_DEFAULT 20
#define MOUSE_I2C_MOVING_SPEED_MIN 1
#define MOUSE_I2C_MOVING_SPEED_MAX 100
#define MOUSE_I2C_MOVING_SPEED_SCALE_DEFAULT 0.7
struct tp_io_ops {
	int (*read)(const struct device *dev,
		    void *data,
		    size_t length);
	int (*command_write)(const struct device *dev,
			  void *data,
			  size_t length);
};

struct tp_config {
	struct i2c_dt_spec i2c;
	const struct tp_io_ops *ops;
	struct gpio_dt_spec int_gpio;

};

struct tp_data {
	struct k_sem sem;
	const struct device *dev;
	struct gpio_callback gpio_cb;
#ifdef CONFIG_TP_TRIGGER_OWN_THREAD
	K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_TP_THREAD_STACK_SIZE);
	struct k_thread thread;
	struct k_sem trig_sem;
#endif
#ifdef CONFIG_TP_TRIGGER_GLOBAL_THREAD
	struct k_work work;
#endif
    // bool button_l_is_held;
    // bool button_m_is_held;
    // bool button_r_is_held;
	bool activating;
	double speed_scale;
    uint8_t moving_speed;
};

