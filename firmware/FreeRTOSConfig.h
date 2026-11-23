#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifndef __ASSEMBLER__
void vAssertCalled(void);
#endif

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
/* This SDK port uses configCPU_CLOCK_HZ for MTIME, not the CPU core.
 * BL616 board_init configures MTIME at 1 MHz while the CPU runs at 320 MHz. */
#define configCPU_CLOCK_HZ                      1000000UL
#define configTICK_RATE_HZ                      1000U
#define configMAX_PRIORITIES                    32U
#define configMINIMAL_STACK_SIZE                128U
#define configTOTAL_HEAP_SIZE                   (160U * 1024U)
#define configMAX_TASK_NAME_LEN                 16U
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2U
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               30U
#define configTIMER_QUEUE_LENGTH                20U
#define configTIMER_TASK_STACK_DEPTH            1024U
#define configUSE_POSIX_ERRNO                   1
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

#define INCLUDE_vTaskPrioritySet         1
#define INCLUDE_uxTaskPriorityGet        1
#define INCLUDE_vTaskDelete              1
#define INCLUDE_vTaskSuspend             1
#define INCLUDE_vTaskDelayUntil          1
#define INCLUDE_vTaskDelay               1
#define INCLUDE_eTaskGetState            1
#define INCLUDE_xTaskGetSchedulerState   1
#define INCLUDE_xTaskGetCurrentTaskHandle 1

#endif
