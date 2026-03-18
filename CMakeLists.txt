# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_USB_HID_BATTERY_REPORTING)
  target_sources(app PRIVATE src/usb_hid_battery.c)
  target_include_directories(app PRIVATE include)
endif()
