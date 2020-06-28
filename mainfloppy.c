#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

#include <interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <serial.h>

#include "filesys.h"

#define FOREGROUND_TASK_PRIO 0
#define BACKGROUND_TASK_PRIO 0

static const char hex2char[16] = "0123456789abcdef";

void FileHexDumpOffset(File_t *f, void *ptr, size_t length, long offset) {
  unsigned char *data = ptr;
  unsigned char *txtdata = ptr;
  char buf[40];
  int last = 0;

  for (size_t i = 0; i < length; i++) {
    if ((i & 15) == 0) {
      FilePrintf(f, "%08x:", offset);
      offset += 16;
    }
    /* optimize the common case */
    unsigned byte = *data++;
    unsigned len = 3;
    last++;
    buf[0] = ' ';
    buf[1] = hex2char[byte >> 4];
    buf[2] = hex2char[byte & 15];
    if ((i & 3) == 3)
      buf[len++] = ' ';
    if ((i & 15) == 15) {
      buf[len++] = '|';
      for (int j = 0; j < 16; j++)
        if (*txtdata >= ' ')
          buf[len++] = *txtdata++;
        else {
          buf[len++] = '.';
          txtdata++;
        }
      buf[len++] = '|';
      buf[len++] = '\n';
      last = 0;
    }
    FileWrite(f, buf, len);
  }
  if (last > 0) {
    int allign = 50 - last * 2 - (last - 1) - (last / 4);
    for (int j = 0; j < allign; j++)
      FilePutChar(f, ' ');
    FilePutChar(f, ' ');
    FilePutChar(f, '|');
    for (int j = 0; j < last; j++)
      if (*txtdata >= ' ')
        FilePutChar(f, *txtdata++);
      else {
        FilePutChar(f, '.');
        txtdata++;
      }
    FilePutChar(f, '|');
  }
  FilePutChar(f, '\n');
}

static void vForegroundTask(File_t *ser) {
  (void)FsMount();
  char *command = pvPortMalloc(100);
  char *str;
  File_t *files[10];
  for (int i = 0; i < 10; i++)
    files[i] = NULL;
  int whence;
  long length, num, curr = -1, off, n;
  char *temp;
  void *base_p;
  for (;;) {
    length = 0;
    do {
      FileRead(ser, command + length, 1);
    } while (command[length++] != '\n');
    command[length - 1] = '\0';
    switch (command[0]) {
      case 'o':
        for (int i = 0; i < 10; i++) {
          if (files[i] == NULL) {
            files[i] = FsOpen(command + 5);
            if (files[i] != NULL)
              FilePrintf(ser, "%d\n", i);
            else
              FilePrintf(ser, "No such file!\n");
            break;
          }
        }
        break;

      case 'c':
        num = strtol(command + 6, NULL, 10);
        if (files[num] != NULL) {
          FileClose(files[num]);
          files[num] = NULL;
        }
        break;

      case 'f':
        curr = strtol(command + 5, NULL, 10);
        break;

      case 'r':
        n = strtol(command + 5, NULL, 10);
        str = pvPortMalloc(n);
        if (curr >= 0) {
          int fileoffset = files[curr]->offset;
          n = FileRead(files[curr], str, n);
          FileHexDumpOffset(ser, str, n, fileoffset);
        } else
          FilePrintf(ser, "Select file!\n");
        vPortFree(str);
        break;

      case 's':
        off = strtol(command + 5, &temp, 10);
        if ((long)(temp + 1 - command) >= length || *(temp + 1) == 'c')
          whence = SEEK_CUR;
        else if (*(temp + 1) == 's')
          whence = SEEK_SET;
        else
          whence = SEEK_END;
        if (curr >= 0)
          FileSeek(files[curr], off, whence);
        else
          FilePrintf(ser, "Select file!\n");
        break;

      case 'l':
        base_p = NULL;
        for (;;) {
          DirEntry_t *direntry = (DirEntry_t *)FsListDir(&base_p);
          if (direntry == NULL) {
            break;
          } else {
            FilePrintf(ser, "%s %d %d ", direntry->name, direntry->start,
                       direntry->size);
            if (direntry->type == 1)
              FilePrintf(ser, "exe\n");
            else
              FilePrintf(ser, "reg\n");
          }
        }

        break;

      default:
        break;
    }
  }
}

static unsigned seed = 123456;

static void vBackgroundTask(__unused File_t *ser) {
  (void)FsMount();
  char *buffer = pvPortMalloc(1000);
  for (;;) {
    int num = rand_r(&seed) % 10;
    void *base_p = NULL;
    DirEntry_t *direntry = NULL;
    for (int i = 0; i <= num || base_p == NULL; i++) {
      direntry = (DirEntry_t *)FsListDir(&base_p);
      if (direntry == NULL) {
        base_p = NULL;
      }
    }
    File_t *f = FsOpen(direntry->name);
    int nbyte;

    do {
      nbyte = rand_r(&seed) % 900 + 100;
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    } while (FileRead(f, buffer, nbyte) > 0);

    FileClose(f);
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
static TaskHandle_t bg_handle1;
static TaskHandle_t bg_handle2;
static TaskHandle_t bg_handle3;

int main(void) {
  portNOP(); /* Breakpoint for simulator. */

  AddIntServer(VertBlankChain, SystemClockTick);

  File_t *ser = SerialOpen(9600);

  xTaskCreate((TaskFunction_t)vForegroundTask, "foreground",
              configMINIMAL_STACK_SIZE, ser, FOREGROUND_TASK_PRIO, &fg_handle);

  xTaskCreate((TaskFunction_t)vBackgroundTask, "background1",
              configMINIMAL_STACK_SIZE, ser, BACKGROUND_TASK_PRIO, &bg_handle1);

  xTaskCreate((TaskFunction_t)vBackgroundTask, "background2",
              configMINIMAL_STACK_SIZE, ser, BACKGROUND_TASK_PRIO, &bg_handle2);

  xTaskCreate((TaskFunction_t)vBackgroundTask, "background3",
              configMINIMAL_STACK_SIZE, ser, BACKGROUND_TASK_PRIO, &bg_handle3);

  FsInit();

  vTaskStartScheduler();

  return 0;
}

void vApplicationIdleHook(void) {
  custom.color[0] = 0;
}
