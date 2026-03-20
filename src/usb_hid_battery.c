/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * USB HID Battery Reporting for Split Keyboards
 * 
 * This module exposes keyboard battery levels using the standard HID Battery System
 * usage page (0x85), which is recognized natively by Windows, macOS, and Linux.
 * 
 * For split keyboards with two halves, we create two HID interfaces:
 * - HID_1: Left half battery
 * - HID_2: Right half battery (requires CONFIG_USB_HID_DEVICE_COUNT >= 3)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/battery.h>
#include <zmk/usb.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_REGISTER(usb_hid_battery, CONFIG_ZMK_LOG_LEVEL);

/*
 * HID Report Descriptor using Battery System usage page (0x85)
 * This format is recognized natively by operating systems.
 *
 * Usage Page 0x85 = Battery System
 * Usage 0x44 = Charging
 * Usage 0x45 = Discharging  
 * Usage 0x65 = AbsoluteStateOfCharge (percentage 0-100)
 * Usage 0x66 = RemainingCapacity
 */
static const uint8_t battery_hid_report_desc[] = {
    0x05, 0x84,       /* Usage Page (Power Device) */
    0x09, 0x04,       /* Usage (UPS) - required container for battery */
    0xA1, 0x01,       /* Collection (Application) */
    
    /* Battery System */
    0x05, 0x85,       /*   Usage Page (Battery System) */
    0x09, 0x10,       /*   Usage (Battery System) */
    0xA1, 0x02,       /*   Collection (Logical) */
    
    /* State of Charge - this is what the OS displays */
    0x09, 0x65,       /*     Usage (AbsoluteStateOfCharge) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x26, 0x64, 0x00, /*     Logical Maximum (100) */
    0x75, 0x08,       /*     Report Size (8 bits) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute) */
    
    /* Charging status */
    0x09, 0x44,       /*     Usage (Charging) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x25, 0x01,       /*     Logical Maximum (1) */
    0x75, 0x01,       /*     Report Size (1 bit) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute) */
    
    /* Discharging status */
    0x09, 0x45,       /*     Usage (Discharging) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x25, 0x01,       /*     Logical Maximum (1) */
    0x75, 0x01,       /*     Report Size (1 bit) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute) */
    
    /* Padding to byte boundary */
    0x75, 0x06,       /*     Report Size (6 bits) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x03,       /*     Input (Constant) - padding */
    
    0xC0,             /*   End Collection (Logical) */
    0xC0,             /* End Collection (Application) */
};

/* Battery report structure - matches HID descriptor */
struct battery_report {
    uint8_t state_of_charge;  /* 0-100% */
    uint8_t status;           /* bit 0 = charging, bit 1 = discharging */
} __packed;

/* Store battery levels for both halves */
static struct battery_report left_battery = {
    .state_of_charge = 0,
    .status = 0x02  /* discharging by default */
};

static struct battery_report right_battery = {
    .state_of_charge = 0,
    .status = 0x02  /* discharging by default */
};

static const struct device *hid_dev_left;
static const struct device *hid_dev_right;
static bool hid_ready_left = false;
static bool hid_ready_right = false;

/* Forward declarations */
static void send_battery_report(const struct device *dev, struct battery_report *report);

/* HID callbacks for left battery */
static void hid_int_ready_left_cb(const struct device *dev) {
    ARG_UNUSED(dev);
}

static int hid_get_report_left_cb(const struct device *dev, 
                                   struct usb_setup_packet *setup,
                                   int32_t *len, uint8_t **data) {
    uint8_t report_type = (setup->wValue >> 8) & 0xFF;
    
    LOG_DBG("Get left battery report: type=%d", report_type);
    
    /* Handle Input report requests */
    if (report_type == 0x01) {
        #if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
        {
            uint8_t level = 0;
            int rc = zmk_split_central_get_peripheral_battery_level(0, &level);
            if (rc == 0) {
                left_battery.state_of_charge = level;
            }
        }
        #endif
        *data = (uint8_t *)&left_battery;
        *len = sizeof(left_battery);
        LOG_DBG("Returning left battery: %d%%", left_battery.state_of_charge);
        return 0;
    }
    
    return -ENOTSUP;
}

static const struct hid_ops hid_ops_left = {
    .int_in_ready = hid_int_ready_left_cb,
    .get_report = hid_get_report_left_cb,
};

/* HID callbacks for right battery */
static void hid_int_ready_right_cb(const struct device *dev) {
    ARG_UNUSED(dev);
}

static int hid_get_report_right_cb(const struct device *dev, 
                                    struct usb_setup_packet *setup,
                                    int32_t *len, uint8_t **data) {
    uint8_t report_type = (setup->wValue >> 8) & 0xFF;
    
    LOG_DBG("Get right battery report: type=%d", report_type);
    
    if (report_type == 0x01) {
        #if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
        {
            uint8_t level = 0;
            int rc = zmk_split_central_get_peripheral_battery_level(1, &level);
            if (rc == 0) {
                right_battery.state_of_charge = level;
            }
        }
        #endif
        *data = (uint8_t *)&right_battery;
        *len = sizeof(right_battery);
        LOG_DBG("Returning right battery: %d%%", right_battery.state_of_charge);
        return 0;
    }
    
    return -ENOTSUP;
}

static const struct hid_ops hid_ops_right = {
    .int_in_ready = hid_int_ready_right_cb,
    .get_report = hid_get_report_right_cb,
};

/* Send battery report over USB interrupt endpoint */
static void send_battery_report(const struct device *dev, struct battery_report *report) {
    if (dev == NULL) {
        return;
    }
    
    /* Only send when USB is connected */
    enum zmk_usb_conn_state usb_state = zmk_usb_get_conn_state();
    if (usb_state != ZMK_USB_CONN_HID) {
        return;
    }
    
    int ret = hid_int_ep_write(dev, (uint8_t *)report, sizeof(*report), NULL);
    if (ret < 0) {
        LOG_WRN("Failed to send battery report: %d", ret);
    } else {
        LOG_DBG("Sent battery report: soc=%d%%, status=0x%02X", 
                report->state_of_charge, report->status);
    }
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
/* Peripheral battery event listener */
static int peripheral_battery_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = 
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    
    LOG_INF("Peripheral %d battery changed: %d%%", ev->source, ev->state_of_charge);
    
    if (ev->source == 0) {
        left_battery.state_of_charge = ev->state_of_charge;
        if (hid_ready_left) {
            send_battery_report(hid_dev_left, &left_battery);
        }
    } else if (ev->source == 1) {
        right_battery.state_of_charge = ev->state_of_charge;
        if (hid_ready_right) {
            send_battery_report(hid_dev_right, &right_battery);
        }
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_hid_battery_peripheral, peripheral_battery_listener);
ZMK_SUBSCRIPTION(usb_hid_battery_peripheral, zmk_peripheral_battery_state_changed);
#endif

/* Initialization */
static int usb_hid_battery_init(void) {
    int ret;
    
    /* Initialize left battery HID device (HID_1) */
    hid_dev_left = device_get_binding("HID_1");
    if (hid_dev_left == NULL) {
        LOG_ERR("Failed to get HID_1 device for left battery");
        return -ENODEV;
    }
    
    usb_hid_register_device(hid_dev_left,
                            battery_hid_report_desc,
                            sizeof(battery_hid_report_desc),
                            &hid_ops_left);
    
    ret = usb_hid_init(hid_dev_left);
    if (ret != 0) {
        LOG_ERR("Failed to initialize left battery HID: %d", ret);
        return ret;
    }
    hid_ready_left = true;
    LOG_INF("Left battery HID initialized (HID_1)");
    
    /* Initialize right battery HID device (HID_2) */
    hid_dev_right = device_get_binding("HID_2");
    if (hid_dev_right == NULL) {
        LOG_WRN("HID_2 not available - right battery won't have native OS support");
        LOG_WRN("Increase CONFIG_USB_HID_DEVICE_COUNT to 3 for dual battery support");
    } else {
        usb_hid_register_device(hid_dev_right,
                                battery_hid_report_desc,
                                sizeof(battery_hid_report_desc),
                                &hid_ops_right);
        
        ret = usb_hid_init(hid_dev_right);
        if (ret != 0) {
            LOG_ERR("Failed to initialize right battery HID: %d", ret);
        } else {
            hid_ready_right = true;
            LOG_INF("Right battery HID initialized (HID_2)");
        }
    }
    
    LOG_INF("USB HID Battery reporting initialized for split keyboard");
    
    return 0;
}

SYS_INIT(usb_hid_battery_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
