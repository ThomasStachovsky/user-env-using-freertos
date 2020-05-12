#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

#include <interrupt.h>
#include <stdio.h>
#include <serial.h>

#include "filesys.h"

#define FOREGROUND_TASK_PRIO 0
#define BACKGROUND_TASK_PRIO 0

static void vForegroundTask(__unused File_t *ser) {
  for (;;) {
  }
  // FilePutChar(ser, '-');
}

static void vBackgroundTask(__unused File_t *ser) {
  for (;;) {
  }

  // FilePutChar(ser, '+');
}

__unused static void vListDirTask(File_t *ser) {
  (void)FsMount();
  void *base_p = NULL;
  for (;;) {
    DirEntry_t *direntry = (DirEntry_t *)FsListDir(&base_p);
    // FilePrintf(ser, "buka %d\n", direntry);
    if (direntry == NULL) {
      base_p = NULL;
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    } else {
      FilePrintf(ser, "%s   (%d)\n", direntry->name, direntry->size);
    }
  }
}

static void SystemClockTickHandler(__unused void *data) {
  /* Increment the system timer value and possibly preempt. */
  uint32_t ulSavedInterruptMask = portSET_INTERRUPT_MASK_FROM_ISR();
  xNeedRescheduleTask = xTaskIncrementTick();
  portCLEAR_INTERRUPT_MASK_FROM_ISR(ulSavedInterruptMask);
}

INTSERVER_DEFINE(SystemClockTick, 10, SystemClockTickHandler, NULL);

static TaskHandle_t fg_handle;
static TaskHandle_t bg_handle;

int main(void) {
  // portNOP(); /* Breakpoint for simulator. */

  AddIntServer(VertBlankChain, SystemClockTick);

  File_t *ser = SerialOpen(9600);

  xTaskCreate((TaskFunction_t)vForegroundTask, "foreground",
              configMINIMAL_STACK_SIZE, ser, FOREGROUND_TASK_PRIO, &fg_handle);

  xTaskCreate((TaskFunction_t)vBackgroundTask, "background",
              configMINIMAL_STACK_SIZE, ser, BACKGROUND_TASK_PRIO, &bg_handle);

  FsInit(ser);

  xTaskCreate((TaskFunction_t)vListDirTask, "listdir", configMINIMAL_STACK_SIZE,
              ser, BACKGROUND_TASK_PRIO, &bg_handle);

  vTaskStartScheduler();

  return 0;
}

void vApplicationIdleHook(void) {
  custom.color[0] = 0;
}
