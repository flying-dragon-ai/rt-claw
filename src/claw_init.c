/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include "claw_os.h"
#include "claw_config.h"
#include "claw_init.h"
#include "gateway.h"
#include "net_service.h"
#include "claw_tools.h"
#include "ai_engine.h"

#ifdef CONFIG_CLAW_SCHED_ENABLE
#include "scheduler.h"
#endif
#ifdef CONFIG_CLAW_SWARM_ENABLE
#include "swarm.h"
#endif
#ifdef CONFIG_CLAW_SKILL_ENABLE
#include "ai_skill.h"
#endif
#ifdef CONFIG_CLAW_FEISHU_ENABLE
#include "feishu.h"
#endif

int claw_init(void)
{
    claw_log_raw("\n");
    claw_log_raw("  +-----------------------------------------+\n");
    claw_log_raw("  |          rt-claw v%s                 |\n", RT_CLAW_VERSION);
    claw_log_raw("  |  Real-Time Claw / Swarm Intelligence    |\n");
    claw_log_raw("  +-----------------------------------------+\n");
    claw_log_raw("\n");

    gateway_init();

#ifdef CONFIG_CLAW_SCHED_ENABLE
    sched_init();
#endif
#ifdef CONFIG_CLAW_SWARM_ENABLE
    swarm_init();
#endif

    net_service_init();

#ifdef CONFIG_CLAW_SWARM_ENABLE
    swarm_start();
#endif
#ifdef CONFIG_CLAW_LCD_ENABLE
    claw_lcd_init();
#endif

    claw_tools_init();
    ai_engine_init();

#ifdef CONFIG_CLAW_SKILL_ENABLE
    ai_skill_init();
#endif
#ifdef CONFIG_CLAW_FEISHU_ENABLE
    feishu_init();
    feishu_start();
#endif

    return CLAW_OK;
}
