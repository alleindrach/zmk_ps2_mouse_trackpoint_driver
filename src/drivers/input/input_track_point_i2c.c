/*
 * Copyright (c) 2024 alleindrach@gmail.com
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zmk_input_track_point_i2c

#include "zmk/input_track_point_i2c.h"
#include <zephyr/sys/util.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <zephyr/input/input.h>
#include <zephyr/settings/settings.h>
#define TP_DATA_UPDATE_INTERVAL_MSEC 1
LOG_MODULE_REGISTER(TP_I2C, CONFIG_ZMK_TP_I2C_LOG_LEVEL);

#define TP_WORK_QUEUE_PRIORITY 2
#define TP_WORK_QUEUE_STACK_SIZE 1024

K_THREAD_STACK_DEFINE(tp_work_queue_stack_area, TP_WORK_QUEUE_STACK_SIZE);
static struct k_work_q tp_work_queue;

static int int_counter=0;

#if DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 0
#error"ydx series tp is not defined in DTS"
#endif
static struct tp_data tp_data= {
    // PS2 devices initialize with this rate
    .moving_speed = MOUSE_I2C_MOVING_SPEED_DEFAULT,
    .dev= NULL,
    .activating=false,
    .speed_scale=MOUSE_I2C_MOVING_SPEED_SCALE_DEFAULT,
};

#define ZMK_MOUSE_I2C_INIT_PRIORITY 90
int tp_read_i2c(const struct device *dev,
		      void *data,
		      size_t length);
int tp_write_i2c(const struct device *dev,
			    void * command,
                size_t length);
int zmk_mouse_i2c_tp_moving_speed_set(uint8_t speed) ;
void zmk_mouse_i2c_activity_move_mouse(int16_t mov_x, int16_t mov_y) ;
static void tp_data_timer_handler(struct k_work *work) ;

static const struct tp_io_ops tp_i2c_ops = {
	.read = tp_read_i2c,
	.command_write = tp_write_i2c
};

static const struct tp_config tp_config= {
    .int_gpio = GPIO_DT_SPEC_INST_GET(0,int_gpios),
    .i2c = I2C_DT_SPEC_INST_GET(0) ,
    .ops = &tp_i2c_ops,		
};

K_WORK_DEFINE(tp_tick_work, tp_data_timer_handler);

static void tp_read_tick_handler(struct k_timer *timer) {
    k_work_submit_to_queue(&tp_work_queue, &tp_tick_work);
}

K_TIMER_DEFINE(tp_tick, tp_read_tick_handler, NULL);





/////////////////////////-----I2C-op-----/////////////////////////
int tp_read_i2c(const struct device *dev,
		      void *data,
		      size_t length)
{
    int ret;
	const struct tp_config *config = dev->config;

 /* 读取6个字节的数据 */
    ret = i2c_read_dt(&config->i2c, data, length);
    if (ret == 0) {
        /* 数据读取成功 */
        // printk("Data read successfully: %x %x %x %x %x %x\n",
        //        ((uint8_t *)data)[0],  ((uint8_t *)data)[1], ((uint8_t *)data)[2],  ((uint8_t *)data)[3],  ((uint8_t *)data)[4],  ((uint8_t *)data)[5]);
    } else {
        /* 数据读取失败 */
        printk("Failed to read data: %d\n", ret);
    }
    return ret;
}

int tp_write_i2c(const struct device *dev,
			    void * command,
                size_t length)
{
	int ret;
    const struct tp_config *config = dev->config;

    /* 写入4个字节的数据 */
    ret = i2c_write_dt(&config->i2c, command, length);
    printk("Data writing:");
    for(int i=0;i<length;i++){
        printk("\t%x",
            ((uint8_t *)command)[i]);
    }
    printk("\n");
    if (ret == 0) {
        /* 数据写入成功 */
        printk("Data written successfully\n");
    } else {
        /* 数据写入失败 */
        printk("Failed to write cmd: %d\n", ret);
    }
    return ret;
}
int tp_wakeup(const struct device *dev)
{
	int ret;
    const struct tp_config *config = dev->config;

    /* 写入4个字节的数据 */
    int cmdlen=sizeof(MOUSE_I2C_CMD_TP_WAKEUP)-1;
    printk("Wakeup cmd len is %d\n",cmdlen);
    ret = tp_write_i2c(dev, MOUSE_I2C_CMD_TP_WAKEUP,cmdlen);
    if (ret == 0) {
        /* 数据写入成功 */
        printk("Data written successfully\n");
    } else {
        /* 数据写入失败 */
        printk("Failed to wakeup: %d\n", ret);
    }
    return ret;
}
int tp_reset(const struct device *dev)
{
	int ret;
    const struct tp_config *config = dev->config;

    /* 写入4个字节的数据 */
    int cmdlen=sizeof(MOUSE_I2C_CMD_TP_RESET)-1;
    printk("Reset cmd len is %d\n",cmdlen);
    ret = tp_write_i2c(dev, MOUSE_I2C_CMD_TP_RESET,cmdlen);
    if (ret == 0) {
        /* 数据写入成功 */
        printk("Data written successfully\n");
    } else {
        /* 数据写入失败 */
        printk("Failed to reset: %d\n", ret);
    }
    return ret;
}

int tp_sleep(const struct device *dev)
{
	int ret;
    const struct tp_config *config = dev->config;

    /* 写入4个字节的数据 */
    int cmdlen=sizeof(MOUSE_I2C_CMD_TP_SLEEP)-1;
    printk("Sleep cmd len is %d\n",cmdlen);
    ret = tp_write_i2c(dev, MOUSE_I2C_CMD_TP_SLEEP,cmdlen);
    if (ret == 0) {
        /* 数据写入成功 */
        printk("Data written successfully\n");
    } else {
        /* 数据写入失败 */
        printk("Failed to Sleep: %d\n", ret);
    }
    return ret;
}

/////////////////////////-----data fetcher-----/////////////////////////
static void tp_data_fetcher(const struct device *dev)
{
	const struct tp_config *config = dev->config;
	struct tp_data *data = dev->data;

	/* Interrupt status register */
	k_sem_take(&data->sem, K_FOREVER);
    uint8_t buffer[7];
    bool moved=false;
    int still_counter=0;
    // k_busy_wait(8*USEC_PER_MSEC);

        int success=config->ops->read(dev,buffer,7);
        if(success==0){
            if((buffer[0] == 0x07) && (buffer[1] == 0x00) && (buffer[2] == 0x01)) 
            { 
                uint8_t mButtonPressed = buffer[3] & 0x04; 
                uint8_t rButtonPressed = buffer[3] & 0x02; 
                uint8_t lButtonPressed = buffer[3] & 0x01; 
                int8_t Delta_x = - buffer[4]; 
                int8_t Delta_y = buffer[5]; 
                if(Delta_x!=0||Delta_y!=0){
                    moved=true;
                    printk("--------move---------\t%d\t%d\t%d\n",Delta_x,Delta_y,still_counter);
                    zmk_mouse_i2c_activity_move_mouse(Delta_x*data->speed_scale,Delta_y*data->speed_scale);
                }else{
                    moved=false;
                    // printk("--------still--------\n");
                }       
            }else{
                // printk("--------inva--------\n");
            }
            still_counter++;
        }else{
                printk("--------erro--------\n");
        }
        
      
    
    k_sem_give(&data->sem);
	
}

static void tp_data_fetcher_irq(const struct device *dev)
{
	const struct tp_config *config = dev->config;
	struct tp_data *data = dev->data;

	k_sem_take(&data->sem, K_FOREVER);
    uint8_t buffer[7];
    bool moved=false;
    int still_counter=0;
    int id=int_counter;

    // k_busy_wait(8*USEC_PER_MSEC);
    printk("--------fetch--------\t%d\n",id);
    while(true){
        int success=config->ops->read(dev,buffer,7);
        if(success==0){
            if((buffer[0] == 0x07) && (buffer[1] == 0x00) && (buffer[2] == 0x01)) 
            { 
                uint8_t mButtonPressed = buffer[3] & 0x04; 
                uint8_t rButtonPressed = buffer[3] & 0x02; 
                uint8_t lButtonPressed = buffer[3] & 0x01; 
                int8_t Delta_x = - buffer[4]; 
                int8_t Delta_y = buffer[5]; 
                if(Delta_x!=0||Delta_y!=0){
                    moved=true;
                    printk("--------move---------\t%d\t%d\t%d\n",Delta_x,Delta_y,still_counter);

                    gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_INACTIVE);

                    zmk_mouse_i2c_activity_move_mouse(Delta_x*data->speed_scale,Delta_y*data->speed_scale);
                }else{
                    moved=false;
                    // printk("--------still--------\n");
                }       
            }
            still_counter++;
        }else{
                printk("--------erro--------\n");
        }
        
        if((data->activating&&!moved)||still_counter>20){
            //之前活动，目前停止
            printk("--------stop--------\t%d\t%d\n",still_counter,id);
            data->activating=false;
            break;
            // k_timer_stop(&tp_tick);
        }else if(!data->activating&& moved){
            printk("--------act!--------\n");
            data->activating=true;
        }
    }
    // int_counter--;
    k_sem_give(&data->sem);
    
	gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_INACTIVE);
}
static void tp_data_timer_handler(struct k_work *work) {
    if(tp_data.dev!=NULL){
        tp_data_fetcher(tp_data.dev);
    }
}

static bool zmk_mouse_i2c_is_non_zero_1d_movement(int16_t speed) { return speed != 0; }

void zmk_mouse_i2c_activity_move_mouse(int16_t mov_x, int16_t mov_y) {
    struct tp_data *data = &tp_data;
    int ret = 0;

    bool have_x = zmk_mouse_i2c_is_non_zero_1d_movement(mov_x);
    bool have_y = zmk_mouse_i2c_is_non_zero_1d_movement(mov_y);
    // printk("mouse delta [%d,%d]\n",mov_x,mov_y);
    if (have_x) {
        ret = input_report_rel(data->dev, INPUT_REL_X, mov_x, !have_y, K_NO_WAIT);
    }
    if (have_y) {
        ret = input_report_rel(data->dev, INPUT_REL_Y, mov_y, true, K_NO_WAIT);
    }
}



static void tp_gpio_interrupt_handler(const struct device *dev,
				   struct gpio_callback *cb,
				   uint32_t pin_mask)
{


    // if (k_timer_status_get(&tp_tick) <= 0){
    //     k_timer_start(&tp_tick, K_NO_WAIT, K_MSEC(TP_DATA_UPDATE_INTERVAL_MSEC));
    // }
    
	struct tp_data *data =
		CONTAINER_OF(cb, struct tp_data, gpio_cb);
	const struct tp_config *config = data->dev->config;

	if ((pin_mask & BIT(config->int_gpio.pin)) == 0U) {
		return;
	}

	gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_DISABLE);

    printk("---gpio callback triggered for pin %d\t %d---\n", pin_mask, int_counter++);
#if defined(CONFIG_TP_TRIGGER_OWN_THREAD)
	k_sem_give(&data->trig_sem);
#elif defined(CONFIG_TP_TRIGGER_GLOBAL_THREAD)
	k_work_submit(&data->work);
#endif 
    gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_INACTIVE);
}


#ifdef CONFIG_TP_TRIGGER_OWN_THREAD
static void tp_thread_main(struct tp_data *data)
{
	while (true) {
		k_sem_take(&data->trig_sem, K_FOREVER);
		tp_data_fetcher_irq(data->dev);
	}
}
#endif

#ifdef CONFIG_TP_TRIGGER_GLOBAL_THREAD
static void tp_work_handler(struct k_work *work)
{
	struct tp_data *data =
		CONTAINER_OF(work, struct tp_data, work);

	tp_data_fetcher_irq(data->dev);
}

#endif


int tp_trigger_init(const struct device *dev)
{
	const struct tp_config *config = dev->config;
	struct tp_data *data = dev->data;
	int ret;

	data->dev = dev;

#if defined(CONFIG_TP_TRIGGER_OWN_THREAD)
	k_sem_init(&data->trig_sem, 0, K_SEM_MAX_LIMIT);
	k_thread_create(&data->thread, data->thread_stack,
			CONFIG_TP_THREAD_STACK_SIZE,
			(k_thread_entry_t)tp_thread_main,
			data, NULL, NULL,
			K_PRIO_COOP(CONFIG_TP_THREAD_PRIORITY),
			0, K_NO_WAIT);
#elif defined(CONFIG_TP_TRIGGER_GLOBAL_THREAD)
	data->work.handler = tp_work_handler;
#endif
    LOG_ERR("Initializing  gpio interrupt callback");
	if (!gpio_is_ready_dt(&config->int_gpio)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT|GPIO_PULL_UP);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&data->gpio_cb, tp_gpio_interrupt_handler,BIT(config->int_gpio.pin));

	ret = gpio_add_callback(config->int_gpio.port, &data->gpio_cb);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_INACTIVE);
	if (ret < 0) {
		return ret;
	}
    LOG_ERR("Gpio interrupt callback ready!");
	return 0;
}

static int tp_init(const struct device *dev)
{
	const struct tp_config *config = dev->config;
	struct tp_data *data = dev->data;

    if (!device_is_ready(config->i2c.bus)) {
        LOG_ERR("I2C bus device not ready");
        return -ENODEV;
    }
    data->dev = dev;

	k_busy_wait(100*USEC_PER_MSEC);

	k_sem_init(&data->sem, 0, 1);

	if (tp_trigger_init(dev)) {
		LOG_ERR("Could not initialize interrupts");
		return -EIO;
	}

	/* The sensor requires us to wait 1 ms after a reset before
	 * attempting further communications.
	 */
	k_busy_wait(10*USEC_PER_MSEC);



    // k_work_queue_start(&tp_work_queue, tp_work_queue_stack_area,
    //                    K_THREAD_STACK_SIZEOF(tp_work_queue_stack_area),
    //                    TP_WORK_QUEUE_PRIORITY, NULL);

    int ret =-1;
    while(ret!=0) {
        ret=tp_reset(dev);
    } 
    zmk_mouse_i2c_tp_moving_speed_set(MOUSE_I2C_MOVING_SPEED_DEFAULT);
    // k_timer_start(&tp_tick, K_NO_WAIT, K_MSEC(TP_DATA_UPDATE_INTERVAL_MSEC));
   
	k_sem_give(&data->sem);
    
	LOG_DBG("Init complete");

	return 0;
}


/*
 * State Saving
 */

#if IS_ENABLED(CONFIG_SETTINGS)


struct k_work_delayable zmk_mouse_i2c_save_work;

int zmk_mouse_i2c_settings_save_setting(char *setting_name, const void *value, size_t val_len) {
    char setting_path[40];
    snprintf(setting_path, sizeof(setting_path), "%s/%s", MOUSE_I2C_SETTINGS_SUBTREE, setting_name);

    LOG_DBG("Saving setting to `%s`", setting_path);
    int err = settings_save_one(setting_path, value, val_len);
    if (err) {
        LOG_ERR("Could not save setting to `%s`: %d", setting_path, err);
    }

    return err;
}

int zmk_mouse_i2c_settings_reset_setting(char *setting_name) {
    char setting_path[40];
    snprintf(setting_path, sizeof(setting_path), "%s/%s", MOUSE_I2C_SETTINGS_SUBTREE, setting_name);

    LOG_DBG("Reseting setting `%s`", setting_path);
    int err = settings_delete(setting_path);
    if (err) {
        LOG_ERR("Could not reset setting `%s`", setting_path);
    }

    return err;
}

static void zmk_mouse_i2c_settings_save_work(struct k_work *work) {
    struct tp_data *data = &tp_data;

    LOG_INF("Saving PS/2 Mouse Settings.");

    zmk_mouse_i2c_settings_save_setting(MOUSE_I2C_ST_TP_MOVING_SPEED, &data->moving_speed,
                                        sizeof(data->moving_speed));
}
#endif

int zmk_mouse_i2c_settings_save() {
#if IS_ENABLED(CONFIG_SETTINGS)
    int ret =
        k_work_reschedule(&zmk_mouse_i2c_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
    return MIN(ret, 0);
#else
    return 0;
#endif
}

int zmk_mouse_i2c_settings_reset() {
    struct tp_data *data = &tp_data;
    LOG_INF("Deleting runtime settings...");
    zmk_mouse_i2c_settings_reset_setting(MOUSE_I2C_ST_TP_MOVING_SPEED);

    LOG_INF("Restoring default settings to TP..");
    zmk_mouse_i2c_tp_moving_speed_set(MOUSE_I2C_MOVING_SPEED_DEFAULT);

    data->speed_scale=MOUSE_I2C_MOVING_SPEED_SCALE_DEFAULT;

    return 0;
}

int zmk_mouse_i2c_settings_log() {
    struct tp_data *data = &tp_data;

    char settings_str[250];

    snprintf(settings_str, sizeof(settings_str), " \n\
&mouse_i2c_conf = { \n\
    tp-speed = <%d>; \n\
}",
             data->moving_speed);

    LOG_INF("Current settings... %s", settings_str);

    return 0;
}

// This function is called when settings are loaded from flash by
// `settings_load_subtree`.
// It's called once for each PS/2 mouse setting that has been stored.
static int zmk_mouse_i2c_settings_restore(const char *name, size_t len, settings_read_cb read_cb,
                                          void *cb_arg) {
    struct tp_data *data = &tp_data;
    const struct tp_config *config = &tp_config;

    uint8_t setting_val;

    if (len != sizeof(setting_val)) {
        LOG_ERR("Could not restore settings %s: Len mismatch", name);

        return -EINVAL;
    }

    int rc = read_cb(cb_arg, &setting_val, sizeof(setting_val));
    if (rc <= 0) {
        LOG_ERR("Could not restore setting %s: %d", name, rc);
        return -EINVAL;
    }


    LOG_INF("Restoring setting %s with value: %d", name, setting_val);

    if (strcmp(name, MOUSE_I2C_ST_TP_MOVING_SPEED) == 0) {

        return zmk_mouse_i2c_tp_moving_speed_set(setting_val);
    } 
    return -EINVAL;
}

struct settings_handler zmk_mouse_i2c_settings_conf = {
    .name = MOUSE_I2C_SETTINGS_SUBTREE,
    .h_set = zmk_mouse_i2c_settings_restore,
};

int zmk_mouse_i2c_settings_init() {
#if IS_ENABLED(CONFIG_SETTINGS)
    LOG_DBG("");

    settings_subsys_init();

    int err = settings_register(&zmk_mouse_i2c_settings_conf);
    if (err) {
        LOG_ERR("Failed to register the I2C mouse settings handler (err %d)", err);
        return err;
    }

    k_work_init_delayable(&zmk_mouse_i2c_save_work, zmk_mouse_i2c_settings_save_work);

    // This will load the settings and then call
    // `zmk_mouse_ps2_settings_restore`, which will set the settings
    settings_load_subtree(MOUSE_I2C_SETTINGS_SUBTREE);
#endif

    return 0;
}




int zmk_mouse_i2c_tp_moving_speed_set(uint8_t speed) {
    struct tp_data *data = &tp_data;

    if (speed < MOUSE_I2C_MOVING_SPEED_MIN ||
        speed > MOUSE_I2C_MOVING_SPEED_MAX) {
        LOG_ERR("Invalid speed value %d. Min: %d; Max: %d", speed,
                MOUSE_I2C_MOVING_SPEED_MIN, MOUSE_I2C_MOVING_SPEED_MAX);
        return 1;
    }
    uint8_t cmd[8];
    memcpy(cmd, MOUSE_I2C_CMD_TP_SET_MOVING_SPEED_VERTICAL, 6);
    cmd[6]=speed;
    cmd[7]=speed;
    int ret = tp_write_i2c(data->dev,cmd,8);
    if(ret != 0){
        LOG_ERR("Set V speed failed  %d.", ret);
        return ret;
    }
    
    memcpy(cmd, MOUSE_I2C_CMD_TP_SET_MOVING_SPEED_HORIZONTAL, 6);
    cmd[6]=speed;
    cmd[7]=speed;
    ret = tp_write_i2c(data->dev,cmd,8);
    if(ret != 0){
        LOG_ERR("Set H speed failed  %d.", ret);
        return ret;
    }

    data->moving_speed = speed;

    LOG_INF("Successfully set TP speed to %d", speed);

    return 0;
}




DEVICE_DT_INST_DEFINE(0, tp_init, NULL, &tp_data, &tp_config,
                      POST_KERNEL, ZMK_MOUSE_I2C_INIT_PRIORITY, NULL);


// #define TP_INT(n)							\
// 		    .int_gpio = GPIO_DT_SPEC_INST_GET(n, int_gpios),


// #define TP_CONFIG_I2C(n)						\
// 		.i2c = I2C_DT_SPEC_INST_GET(n) ,		\
// 		.ops = &tp_i2c_ops,			

// // Depends on the UART and PS2 init priorities, which are 55 and 45 by default

// #define TP_INIT(n)						\
// 	static const struct tp_config tp_config_##n = {	\
// 		TP_CONFIG_I2C(n)			\
//         TP_INT(n)						\
// 	};								\
// 									\
// 	static struct tp_data tp_data_##n;			\
// 									\
// 	DEVICE_DT_INST_DEFINE(n,					\
// 				     tp_init,			\
// 				     NULL,				\
// 				     &tp_data_##n,		\
// 				     &tp_config_##n,		\
// 				     POST_KERNEL,			\
// 				     ZMK_MOUSE_I2C_INIT_PRIORITY,	\
// 				     NULL);

// DT_INST_FOREACH_STATUS_OKAY(TP_INIT)





// DEVICE_DT_INST_DEFINE(0, &zmk_mouse_ps2_init, NULL, &zmk_mouse_ps2_data, &zmk_mouse_ps2_config,
//                       POST_KERNEL, ZMK_MOUSE_PS2_INIT_PRIORITY, NULL);
