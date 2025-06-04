# Copyright (c) 2022 The ZMK Contributors
 # SPDX-License-Identifier: MIT

 target_sources_ifdef(CONFIG_ZMK_INPUT_MOUSE_PS2 app PRIVATE input_mouse_ps2.c)
 target_sources_ifdef(CONFIG_ZMK_INPUT_TRACK_POINT_I2C app PRIVATE input_track_point_i2c.c)