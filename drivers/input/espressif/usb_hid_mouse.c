/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * USB HID Mouse driver — ESP32-S3 USB OTG via TinyUSB.
 *
 * Registers as a standard 3-button mouse with scroll wheel.
 * Uses esp_tinyusb component (ESP-IDF managed component).
 */

#include "drivers/input/espressif/usb_hid_mouse.h"
#include "osal/claw_os.h"

#define TAG "usb_hid_mouse"

#ifdef CONFIG_RTCLAW_USB_HID_MOUSE

#include "tinyusb.h"
#include "class/hid/hid_device.h"

/* HID Report ID */
#define REPORT_ID_MOUSE  1

/* ------------------------------------------------------------------ */
/* HID Descriptors                                                    */
/* ------------------------------------------------------------------ */

/*
 * Standard HID mouse report descriptor:
 * 3 buttons + X/Y relative movement + vertical scroll wheel
 */
static const uint8_t s_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE))
};

#define TUSB_DESC_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

/*
 * USB configuration descriptor:
 * - 1 configuration, 1 interface
 * - Remote wakeup enabled, 100mA max power
 * - HID endpoint: EP1 IN, 16 bytes, 10ms polling interval
 */
static const uint8_t s_hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(s_hid_report_descriptor),
                        0x81, 16, 10),
};

/* String descriptors */
static const char *s_hid_string_descriptor[] = {
    (const char[]){0x09, 0x04},  /* Language: English (US) */
    "rt-claw",                    /* Manufacturer */
    "rt-claw HID Mouse",         /* Product */
    "000001",                     /* Serial Number */
    "HID Mouse Interface",       /* HID Interface */
};

/* ------------------------------------------------------------------ */
/* TinyUSB Callbacks                                                  */
/* ------------------------------------------------------------------ */

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

/* ------------------------------------------------------------------ */
/* Wait helper                                                        */
/* ------------------------------------------------------------------ */

/*
 * Wait for HID device to become ready (mounted + endpoint idle).
 * Returns 1 if ready, 0 on timeout.
 */
static int hid_wait_ready(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t poll_ms = 5;

    while (elapsed < timeout_ms) {
        if (tud_hid_ready()) {
            return 1;
        }
        claw_delay_ms(poll_ms);
        elapsed += poll_ms;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

claw_err_t usb_hid_mouse_init(void)
{
    CLAW_LOGI(TAG, "initializing USB HID mouse (TinyUSB)");

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,  /* use Kconfig defaults */
        .string_descriptor = s_hid_string_descriptor,
        .string_descriptor_count =
            sizeof(s_hid_string_descriptor) /
            sizeof(s_hid_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = s_hid_configuration_descriptor,
    };

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        CLAW_LOGE(TAG, "TinyUSB install failed: %d", ret);
        return CLAW_ERR_GENERIC;
    }

    CLAW_LOGI(TAG, "USB HID mouse ready (waiting for host)");
    return CLAW_OK;
}

claw_err_t usb_hid_mouse_move(int8_t dx, int8_t dy)
{
    if (!hid_wait_ready(100)) {
        CLAW_LOGW(TAG, "HID not ready, move ignored");
        return CLAW_ERR_GENERIC;
    }

    tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, dx, dy, 0, 0);
    return CLAW_OK;
}

claw_err_t usb_hid_mouse_click(uint8_t buttons)
{
    if (!hid_wait_ready(100)) {
        CLAW_LOGW(TAG, "HID not ready, click ignored");
        return CLAW_ERR_GENERIC;
    }

    /* Press */
    tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, 0, 0, 0, 0);

    /* Wait for report to send, then release */
    if (!hid_wait_ready(50)) {
        return CLAW_ERR_GENERIC;
    }
    claw_delay_ms(50);
    tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, 0, 0, 0, 0);

    return CLAW_OK;
}

claw_err_t usb_hid_mouse_scroll(int8_t delta)
{
    if (!hid_wait_ready(100)) {
        CLAW_LOGW(TAG, "HID not ready, scroll ignored");
        return CLAW_ERR_GENERIC;
    }

    tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, 0, 0, delta, 0);
    return CLAW_OK;
}

int usb_hid_mouse_is_ready(void)
{
    return tud_mounted() && tud_hid_ready();
}

/* ------------------------------------------------------------------ */
/* OOP driver registration                                            */
/* ------------------------------------------------------------------ */

#include "claw/core/claw_driver.h"

static claw_err_t usb_hid_mouse_probe(struct claw_driver *drv)
{
    (void)drv;
    return usb_hid_mouse_init();
}

static const struct claw_driver_ops usb_hid_mouse_ops = {
    .probe  = usb_hid_mouse_probe,
    .remove = NULL,
};

static struct claw_driver usb_hid_mouse_drv = {
    .name  = "usb_hid_mouse",
    .ops   = &usb_hid_mouse_ops,
    .state = CLAW_DRV_REGISTERED,
};

CLAW_DRIVER_REGISTER(usb_hid_mouse, &usb_hid_mouse_drv);

#else /* !CONFIG_RTCLAW_USB_HID_MOUSE */

claw_err_t usb_hid_mouse_init(void)
{
    return CLAW_ERR_INVALID;
}

claw_err_t usb_hid_mouse_move(int8_t dx, int8_t dy)
{
    (void)dx;
    (void)dy;
    return CLAW_ERR_INVALID;
}

claw_err_t usb_hid_mouse_click(uint8_t buttons)
{
    (void)buttons;
    return CLAW_ERR_INVALID;
}

claw_err_t usb_hid_mouse_scroll(int8_t delta)
{
    (void)delta;
    return CLAW_ERR_INVALID;
}

int usb_hid_mouse_is_ready(void)
{
    return 0;
}

#endif /* CONFIG_RTCLAW_USB_HID_MOUSE */
