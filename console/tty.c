#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

#include <interrupt.h>
#include <stdio.h>

#include <bitmap.h>
#include <sprite.h>
#include <font.h>
#include <file.h>
#include <string.h>
#include <limits.h>

#include "console.h"
#include "event.h"
#include "tty.h"

#define ttyDRIVER_TASK_PRIORITY 3
#define BACKSPACE_PRESSED(ev)                                                  \
  ev.key.ascii == '\b' && (ev.key.modifier & MOD_PRESSED)
#define KILL_PRESSED(ev)                                                       \
  (ev.key.code == KEY_U) &&                                                    \
    ((ev.key.modifier & (MOD_CONTROL | MOD_PRESSED)) ==                        \
     (MOD_CONTROL | MOD_PRESSED))
#define PRINTABLE_CHAR_PRESSED(ev)                                             \
  (ev.key.ascii >= 32 || ev.key.ascii == '\n') &&                              \
    (ev.key.modifier & MOD_PRESSED)

static long TtyWrite(File_t *f, const char *buf, size_t nbyte);
static void TtyDriver(void *f);
static long TtyRead(File_t *f, void *buf, size_t nbyte);

static FileOps_t TtyOps = {
  .write = (FileWrite_t)TtyWrite, .close = TtyClose, .read = TtyRead};
static xTaskHandle tty_driver_handle;

File_t *TtyOpen(void) {
  taskENTER_CRITICAL();
  static File_t f = {.ops = &TtyOps};
  f.usecount++;
  if (f.usecount == 1) {
    QueuesInit();
    KeyboardInit(PushKeyEventFromISR);
    xTaskCreate(TtyDriver, "tty_driver", configMINIMAL_STACK_SIZE, &f,
                ttyDRIVER_TASK_PRIORITY, &tty_driver_handle);
    InitTtyDriverHandle(tty_driver_handle);
  }
  taskEXIT_CRITICAL();
  return &f;
}

void TtyClose(File_t *f) {
  taskENTER_CRITICAL();
  f->usecount--;
  if (f->usecount <= 0) {
    QueuesKill();
    KeyboardKill();
    vTaskDelete(tty_driver_handle);
  }
  taskEXIT_CRITICAL();
}

static long TtyWrite(__unused File_t *f, const char *buf, size_t nbyte) {
  uint32_t nbytes_done;
  ReadWriteEvent_t ev = {(char *)buf, nbyte, xTaskGetCurrentTaskHandle()};
  PushEventTtyWrite(&ev);
  xTaskNotifyWait(0xffffffff, 0, &nbytes_done, ULONG_MAX);
  return nbytes_done;
}

static long TtyRead(__unused File_t *f, void *buf, size_t nbyte) {
  uint32_t nbytes_done;
  ReadWriteEvent_t ev = {buf, nbyte, xTaskGetCurrentTaskHandle()};
  PushEventTtyRead(&ev);
  xTaskNotifyWait(0xffffffff, 0, &nbytes_done, portMAX_DELAY);
  return nbytes_done;
}

static void TtyDriver(__unused void *f) {
  char linebuffer[MAX_CANON];
  int linebuffer_last = 0;
  Event_t ev;
  ReadWriteEvent_t rwev;

  for (;;) {
    /* Wait for any notification */
    if (!xTaskNotifyWait(0xffffffff, 0xffffffff, NULL, portMAX_DELAY))
      continue;

    /* Check if there's something from other tasks and print it */
    if (PopEventTtyWrite(&rwev)) {
      for (size_t i = 0; i < rwev.nbyte; i++)
        ConsolePutChar(*(char *)(rwev.buf)++);
      ConsoleDrawCursor();
      xTaskNotify(rwev.taskhandle, rwev.nbyte, eSetValueWithOverwrite);
    }

    /* Check if there are pressed keys on the keyboard */
    while (PopKeyEvent(&ev)) {
      if (ev.type == EV_KEY) {

        /* If backspace pressed, erase last character */
        if (BACKSPACE_PRESSED(ev)) {
          EraseChar();
          ConsoleDrawCursor();
          if (linebuffer_last > 0)
            linebuffer_last--;
        }

        /* If Ctrl+U pressed, kill the line */
        else if (KILL_PRESSED(ev)) {
          KillLine();
          ConsoleDrawCursor();
          linebuffer_last = 0;
        }

        /* If printable character pressed, print it and add to buffer */
        else if (PRINTABLE_CHAR_PRESSED(ev)) {
          ConsolePutChar(ev.key.ascii);
          linebuffer[linebuffer_last++] = ev.key.ascii;
          ConsoleDrawCursor();

          /* If buffer full or newline pressed, check if there is a task waiting
           * for text and copy the text to its memory */
          if (linebuffer_last >= MAX_CANON || ev.key.code == KEY_RETURN) {
            if (PopEventTtyRead(&rwev)) {
              memcpy(rwev.buf, linebuffer, linebuffer_last);
              xTaskNotify(rwev.taskhandle, linebuffer_last,
                          eSetValueWithOverwrite);
            }
            linebuffer_last = 0;
          }
        }
      }
    }
  }
}
