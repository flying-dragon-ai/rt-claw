# rt-claw

**中文** | [English](README.md)

Real-Time Claw — 受 OpenClaw 启发，面向嵌入式设备的智能助手。

通过 OSAL 支持多 RTOS，以组网节点构建蜂群智能。

## 核心理念

rt-claw 通过低成本嵌入式节点与蜂群组网，让智能从云端走向边缘。
每一个节点都可以感知世界、与其他节点协作，并实时执行控制任务。

## 架构

```
+---------------------------------------------------+
|                rt-claw Application                |
|   gateway  |  swarm  |  net_service  |  ai_engine |
+---------------------------------------------------+
|               claw_os.h  (OSAL API)               |
+-----------------+---------------------------------+
| FreeRTOS (IDF)  |          RT-Thread              |
+-----------------+---------------------------------+
| ESP32-C3        |  QEMU vexpress-a9               |
| WiFi / BLE      |  Ethernet / UART                |
+-----------------+---------------------------------+
```

## 支持平台

| 平台 | RTOS | 构建系统 | 状态 |
|------|------|---------|------|
| ESP32-C3 | ESP-IDF + FreeRTOS | CMake (idf.py) | 开发中 |
| QEMU vexpress-a9 | RT-Thread | SCons | 可用 |

## 快速开始

### QEMU vexpress-a9 (RT-Thread)

```bash
# 依赖：arm-none-eabi-gcc, qemu-system-arm, scons
cd platform/qemu-a9-rtthread
scons -j$(nproc)
../../tools/qemu-run.sh
```

### ESP32-C3 (ESP-IDF)

```bash
# 依赖：ESP-IDF v5.x, Espressif QEMU
cd platform/esp32c3
idf.py set-target esp32c3
idf.py build
idf.py qemu monitor         # QEMU 仿真
idf.py -p /dev/ttyUSB0 flash monitor  # 真实硬件
```

## 项目结构

```
rt-claw/
├── osal/                    # 操作系统抽象层
│   ├── include/claw_os.h   #   统一 RTOS API
│   ├── freertos/            #   FreeRTOS 实现
│   └── rtthread/            #   RT-Thread 实现
├── src/                     # 平台无关核心代码
│   ├── core/gateway.*       #   消息路由
│   ├── services/swarm/      #   蜂群智能
│   ├── services/net/        #   网络服务
│   └── services/ai/         #   AI 推理引擎
├── platform/
│   ├── esp32c3/             # ESP-IDF 工程 (CMake)
│   └── qemu-a9-rtthread/   # RT-Thread BSP (SCons)
├── vendor/
│   ├── freertos/            # FreeRTOS-Kernel (子模块)
│   └── rt-thread/           # RT-Thread (子模块)
├── scripts/                 # 代码风格与开发工具
└── tools/                   # 构建与启动脚本
```

## 文档

- [编码风格](docs/zh/coding-style.md)
- [贡献指南](docs/zh/contributing.md)
- [架构设计](docs/zh/architecture.md)
- [ESP32-C3 QEMU 指南](docs/zh/esp32c3-qemu.md)

## 许可证

MIT
