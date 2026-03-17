/* SPDX-License-Identifier: MIT */
/*
 * FreeRTOS configuration for QEMU Xilinx Zynq-7000 (Cortex-A9)
 *
 * Based on the FreeRTOS official Zynq QEMU Demo configuration,
 * adapted for rt-claw embedded AI assistant platform.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "xparameters.h"

/*-----------------------------------------------------------
 * Application-specific definitions
 *----------------------------------------------------------*/

/* Scheduling */
#define configUSE_PREEMPTION                        1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION      1
#define configUSE_TICKLESS_IDLE                      0
#define configCPU_CLOCK_HZ                          100000000UL
#define configTICK_RATE_HZ                          1000
#define configMAX_PRIORITIES                        8
#define configMINIMAL_STACK_SIZE                     256
#define configMAX_TASK_NAME_LEN                     16
#define configIDLE_SHOULD_YIELD                     1

/* Memory */
#define configTOTAL_HEAP_SIZE                       (512 * 1024)
#define configSUPPORT_STATIC_ALLOCATION             0
#define configSUPPORT_DYNAMIC_ALLOCATION            1

/* Data types */
#define configUSE_16_BIT_TICKS                      0
#define configTASK_RETURN_ADDRESS                    NULL

/* Hooks */
#define configUSE_IDLE_HOOK                         0
#define configUSE_TICK_HOOK                         0
#define configCHECK_FOR_STACK_OVERFLOW              2
#define configUSE_MALLOC_FAILED_HOOK                1

/* Synchronization */
#define configUSE_MUTEXES                           1
#define configUSE_RECURSIVE_MUTEXES                 1
#define configUSE_COUNTING_SEMAPHORES               1
#define configQUEUE_REGISTRY_SIZE                   8

/* Software timers */
#define configUSE_TIMERS                            1
#define configTIMER_TASK_PRIORITY                    (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                     10
#define configTIMER_TASK_STACK_DEPTH                 (configMINIMAL_STACK_SIZE * 2)

/* Trace and stats */
#define configUSE_TRACE_FACILITY                    1
#define configGENERATE_RUN_TIME_STATS               0
#define configUSE_STATS_FORMATTING_FUNCTIONS         0

/* Co-routines (unused) */
#define configUSE_CO_ROUTINES                       0
#define configMAX_CO_ROUTINE_PRIORITIES             2

/* FPU support: all tasks save/restore FPU context */
#define configUSE_TASK_FPU_SUPPORT                  2

/* QEMU marker */
#define configUSING_QEMU                            1

/*-----------------------------------------------------------
 * Optional function inclusion
 *----------------------------------------------------------*/

#define INCLUDE_vTaskPrioritySet                     1
#define INCLUDE_uxTaskPriorityGet                    1
#define INCLUDE_vTaskDelete                          1
#define INCLUDE_vTaskCleanUpResources                1
#define INCLUDE_vTaskSuspend                         1
#define INCLUDE_vTaskDelayUntil                      1
#define INCLUDE_vTaskDelay                           1
#define INCLUDE_xTimerPendFunctionCall               1
#define INCLUDE_eTaskGetState                        1

/*-----------------------------------------------------------
 * Zynq-7000 GIC (Generic Interrupt Controller) configuration
 *----------------------------------------------------------*/

#define configINTERRUPT_CONTROLLER_BASE_ADDRESS      XPAR_PS7_SCUGIC_0_DIST_BASEADDR
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET (-0xf00)
#define configUNIQUE_INTERRUPT_PRIORITIES            32
#define configMAX_API_CALL_INTERRUPT_PRIORITY         18

/*-----------------------------------------------------------
 * Tick interrupt setup (provided by platform port)
 *----------------------------------------------------------*/

void vConfigureTickInterrupt(void);
#define configSETUP_TICK_INTERRUPT() vConfigureTickInterrupt()

void vClearTickInterrupt(void);
#define configCLEAR_TICK_INTERRUPT() vClearTickInterrupt()

/*-----------------------------------------------------------
 * Assert and debug
 *----------------------------------------------------------*/

void vAssertCalled(const char *pcFile, unsigned long ulLine);
#define configASSERT(x)                                         \
    do {                                                        \
        if ((x) == 0) {                                         \
            vAssertCalled(__FILE__, __LINE__);                   \
        }                                                       \
    } while (0)

#endif /* FREERTOS_CONFIG_H */
