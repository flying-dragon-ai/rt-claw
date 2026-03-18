/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OTA platform stub for Linux — OTA not supported.
 */

#include "claw_ota.h"
#include "claw_config.h"
#include "osal/claw_os.h"

#define TAG "ota_linux"

int claw_ota_platform_init(void)
{
    CLAW_LOGI(TAG, "OTA not supported on Linux");
    return CLAW_OK;
}

int claw_ota_do_update(const char *url,
                       claw_ota_progress_cb progress)
{
    (void)url;
    (void)progress;
    CLAW_LOGW(TAG, "OTA update not supported on Linux");
    return CLAW_ERROR;
}

int claw_ota_rollback(void)
{
    return CLAW_ERROR;
}

int claw_ota_mark_valid(void)
{
    return CLAW_OK;
}

const char *claw_ota_running_version(void)
{
    return RT_CLAW_VERSION;
}

int claw_ota_supported(void)
{
    return 0;
}
