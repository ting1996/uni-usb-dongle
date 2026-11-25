#ifndef TEST_TASK_H
#define TEST_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef enum {
    eRunning = 0,
    eReady,
    eBlocked,
    eSuspended,
    eDeleted,
    eInvalid
} eTaskState;
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void vTaskSuspend(TaskHandle_t task);
void vTaskResume(TaskHandle_t task);
eTaskState eTaskGetState(TaskHandle_t task);
BaseType_t xTaskCreate(void (*entry)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle);
uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);
BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify);
void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify,
                            BaseType_t *pxHigherPriorityTaskWoken);
#define portYIELD_FROM_ISR(x) ((void)(x))
#endif
