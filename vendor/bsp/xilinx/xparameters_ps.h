/* SPDX-License-Identifier: MIT */
/*
 * Zynq-7000 PS peripheral parameters for QEMU.
 * Supplement to xparameters.h — defines XPAR values used by emacps driver.
 */

#ifndef XPARAMETERS_PS_H
#define XPARAMETERS_PS_H

/* ---- EMAC PS (Cadence GEM) ---- */

#define XPAR_XEMACPS_NUM_INSTANCES          2U

/* GEM 0 */
#define XPAR_XEMACPS_0_DEVICE_ID            0U
#define XPAR_XEMACPS_0_BASEADDR             0xE000B000U
#define XPAR_XEMACPS_0_HIGHADDR             0xE000BFFFU
#define XPAR_XEMACPS_0_INTR                 54U
#define XPAR_XEMACPS_0_ENET_CLK_FREQ_HZ     125000000U
#define XPAR_XEMACPS_0_ENET_SLCR_1000MBPS   0x003C0008U
#define XPAR_XEMACPS_0_ENET_SLCR_100MBPS    0x00000008U
#define XPAR_XEMACPS_0_ENET_SLCR_10MBPS     0x00000008U
#define XPAR_XEMACPS_0_ENET_TSU_CLK_FREQ_HZ 0U
#define XPAR_XEMACPS_0_IS_CACHE_COHERENT     0U

/* GEM 0 SLCR clock dividers (used by xemacps_g.c) */
#define XPAR_XEMACPS_0_ENET_SLCR_1000Mbps_DIV0   8U
#define XPAR_XEMACPS_0_ENET_SLCR_1000Mbps_DIV1   1U
#define XPAR_XEMACPS_0_ENET_SLCR_100Mbps_DIV0    8U
#define XPAR_XEMACPS_0_ENET_SLCR_100Mbps_DIV1    5U
#define XPAR_XEMACPS_0_ENET_SLCR_10Mbps_DIV0     8U
#define XPAR_XEMACPS_0_ENET_SLCR_10Mbps_DIV1     50U

/* GEM 1 */
#define XPAR_XEMACPS_1_DEVICE_ID            1U
#define XPAR_XEMACPS_1_BASEADDR             0xE000C000U
#define XPAR_XEMACPS_1_HIGHADDR             0xE000CFFFU
#define XPAR_XEMACPS_1_INTR                 77U
#define XPAR_XEMACPS_1_ENET_CLK_FREQ_HZ     125000000U
#define XPAR_XEMACPS_1_IS_CACHE_COHERENT     0U

/* GEM 1 SLCR clock dividers */
#define XPAR_XEMACPS_1_ENET_SLCR_1000Mbps_DIV0   8U
#define XPAR_XEMACPS_1_ENET_SLCR_1000Mbps_DIV1   1U
#define XPAR_XEMACPS_1_ENET_SLCR_100Mbps_DIV0    8U
#define XPAR_XEMACPS_1_ENET_SLCR_100Mbps_DIV1    5U
#define XPAR_XEMACPS_1_ENET_SLCR_10Mbps_DIV0     8U
#define XPAR_XEMACPS_1_ENET_SLCR_10Mbps_DIV1     50U

/* Aliases used by FreeRTOS+TCP Zynq driver */
#define XPAR_PS7_ETHERNET_0_ENET_CLK_FREQ_HZ  XPAR_XEMACPS_0_ENET_CLK_FREQ_HZ

/* Sleep timer — emacps PHY autonegotiation uses usleep */
#define XPAR_PS7_CORTEXA9_0_CPU_CLK_FREQ_HZ   XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ

/* SLCR (System Level Control Register) base for emacps clock config */
#define XPAR_PS7_SLCR_0_BASEADDR               0xF8000000U

/* Legacy GIC aliases used by FreeRTOS+TCP Zynq DMA driver */
#define XPAR_SCUGIC_CPU_BASEADDR                XPAR_PS7_SCUGIC_0_BASEADDR
#define XPAR_SCUGIC_DIST_BASEADDR               XPAR_PS7_SCUGIC_0_DIST_BASEADDR

/* System control base (SLCR, used by PHY speed config) */
#define XPS_SYS_CTRL_BASEADDR                   XPAR_PS7_SLCR_0_BASEADDR

#endif /* XPARAMETERS_PS_H */
