/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Global compile-time configuration for rt-claw.
 */

#ifndef __CLAW_CONFIG_H__
#define __CLAW_CONFIG_H__

#define RT_CLAW_VERSION         "0.1.0"

/* Gateway */
#define CLAW_GW_MSG_POOL_SIZE   16
#define CLAW_GW_MSG_MAX_LEN     256
#define CLAW_GW_THREAD_STACK    4096
#define CLAW_GW_THREAD_PRIO     15

/* Swarm */
#define CLAW_SWARM_MAX_NODES        32
#define CLAW_SWARM_HEARTBEAT_MS     5000
#define CLAW_SWARM_TIMEOUT_MS       15000
#define CLAW_SWARM_PORT             5300
#define CLAW_SWARM_THREAD_STACK     4096
#define CLAW_SWARM_THREAD_PRIO      12

/* Scheduler */
#define CLAW_SCHED_MAX_TASKS        8
#define CLAW_SCHED_TICK_MS          1000
#define CLAW_SCHED_THREAD_STACK     8192
#define CLAW_SCHED_THREAD_PRIO      10

/*
 * Non-ESP-IDF platforms: enable all modules by default.
 * ESP-IDF uses Kconfig (CONFIG_CLAW_*) to toggle modules.
 */
#ifndef CLAW_PLATFORM_ESP_IDF

#ifndef CONFIG_CLAW_SWARM_ENABLE
#define CONFIG_CLAW_SWARM_ENABLE    1
#endif
#ifndef CONFIG_CLAW_SCHED_ENABLE
#define CONFIG_CLAW_SCHED_ENABLE    1
#endif
#ifndef CONFIG_CLAW_SKILL_ENABLE
#define CONFIG_CLAW_SKILL_ENABLE    1
#endif
#ifndef CONFIG_CLAW_TOOL_GPIO
#define CONFIG_CLAW_TOOL_GPIO       1
#endif
#ifndef CONFIG_CLAW_TOOL_SYSTEM
#define CONFIG_CLAW_TOOL_SYSTEM     1
#endif
#ifndef CONFIG_CLAW_TOOL_SCHED
#define CONFIG_CLAW_TOOL_SCHED      1
#endif

/* AI engine defaults (override with -D at build time) */
#ifndef CONFIG_CLAW_AI_API_KEY
#define CONFIG_CLAW_AI_API_KEY      ""
#endif
#ifndef CONFIG_CLAW_AI_API_URL
#define CONFIG_CLAW_AI_API_URL      "https://api.anthropic.com/v1/messages"
#endif
#ifndef CONFIG_CLAW_AI_MODEL
#define CONFIG_CLAW_AI_MODEL        "claude-sonnet-4-6"
#endif
#ifndef CONFIG_CLAW_AI_MAX_TOKENS
#define CONFIG_CLAW_AI_MAX_TOKENS   1024
#endif
#ifndef CONFIG_CLAW_AI_MEMORY_MAX_MSGS
#define CONFIG_CLAW_AI_MEMORY_MAX_MSGS 20
#endif

#endif /* !CLAW_PLATFORM_ESP_IDF */

#endif /* __CLAW_CONFIG_H__ */
