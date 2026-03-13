/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Feishu (Lark) IM integration — receive messages via WebSocket
 * long connection, forward to AI engine, reply via HTTP API.
 */

#ifndef CLAW_SERVICES_IM_FEISHU_H
#define CLAW_SERVICES_IM_FEISHU_H

int feishu_init(void);
int feishu_start(void);

#endif /* CLAW_SERVICES_IM_FEISHU_H */
