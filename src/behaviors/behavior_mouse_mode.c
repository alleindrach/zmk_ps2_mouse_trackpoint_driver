#define DT_DRV_COMPAT zmk_behavior_mouse_mode

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>


#include <zmk/input_mouse_ps2.h>
#include <zmk/mouse_mode.h>

static int zmk_behavior_mouse_mode = 0;

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
   
    LOG_WRN("===== to scroll mode=======");                                    
    zmk_behavior_mouse_mode=MODE_SCROLL;
    return -ENOTSUP;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    LOG_WRN("===== to move mode=======");
    zmk_behavior_mouse_mode=MODE_MOVE;
    return -ENOTSUP;
}

// Initialization Function

static int zmk_behavior_mouse_mode_init(const struct device *dev) { 

        zmk_behavior_mouse_mode=MODE_MOVE;
        return 0; 
     };
int get_zmk_behavior_mouse_mode() { 
        
        return zmk_behavior_mouse_mode; 
     };

static const struct behavior_driver_api zmk_behavior_mouse_mode_driver_api = {
    .binding_pressed = on_keymap_binding_pressed, .binding_released = on_keymap_binding_released};

BEHAVIOR_DT_INST_DEFINE(0, zmk_behavior_mouse_mode_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &zmk_behavior_mouse_mode_driver_api);
