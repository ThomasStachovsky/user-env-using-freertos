#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

#include <interrupt.h>
#include <stdio.h>

#include <bitmap.h>
#include <sprite.h>
#include <font.h>
#include <file.h>
#include <string.h>

#include "console.h"
#include "event.h"
#include "tty.h"

static long TtyWrite(File_t *f, const char *buf, size_t nbyte);
static void TtyDriver(void *f);
static long TtyRead(File_t *f, void *buf, size_t nbyte);

static FileOps_t TtyOps = {
  .write = (FileWrite_t)TtyWrite, .close = TtyClose, .read = TtyRead};
static xTaskHandle tty_driver_handle;

/* XXX: Nie potrafie wpasc na pomysl, jak uniknac race condition przy uzyciu
 * jedynie kolejek i nie imitujac nimi innych srodkow synchronizacji */
File_t *TtyOpen(void) {
  static File_t f = {.ops = &TtyOps};
  f.usecount++;
  if (f.usecount == 1) {
    QueuesInit();
    KeyboardInit(PushKeyEventFromISR);
    xTaskCreate(TtyDriver, "tty_driver", configMINIMAL_STACK_SIZE, &f,
                ttyDRIVER_TASK_PRIORITY, &tty_driver_handle);
    InitTtyDriverHandle(tty_driver_handle);
  }
  return &f;
}

void TtyClose(File_t *f) {
  f->usecount--;
  if (f->usecount <= 0) {
    QueuesKill();
    KeyboardKill();
    vTaskDelete(tty_driver_handle);
  }
}

static long TtyWrite(__unused File_t *f, const char *buf, size_t nbyte) {
  uint32_t nbytes_done;
  ReadWriteEvent_t ev = {(char *)buf, nbyte, xTaskGetCurrentTaskHandle()};
  PushEventTtyWrite(&ev);
  xTaskNotifyWait(0xffffffff, 0, &nbytes_done, portMAX_DELAY);
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
  bool ctrl_pressed = pdFALSE;
  int linebuffer_last = 0;
  Event_t ev;
  ReadWriteEvent_t rwev;

  for (;;) {
    if (!xTaskNotifyWait(0xffffffff, 0xffffffff, NULL, portMAX_DELAY))
      continue;
    if (PopEventTtyWrite(&rwev)) {
      for (size_t i = 0; i < rwev.nbyte; i++)
        ConsolePutChar(*(char *)(rwev.buf)++);
      ConsoleDrawCursor();
      xTaskNotify(rwev.taskhandle, rwev.nbyte, eSetValueWithOverwrite);
    }
    while (PopKeyEvent(&ev)) {
      if (ev.type == EV_KEY) {
        ctrl_pressed = (ev.key.code == KEY_CONTROL) && (ev.key.modifier == 0x03)
                         ? pdTRUE
                         : pdFALSE;

        if ((ev.key.code == KEY_U) && (ev.key.modifier != 0x00) &&
            ctrl_pressed) {
          // erase line
        } else if (ev.key.modifier == 0x01 || ev.key.modifier == 0x03 ||
                   ev.key.modifier == 0x05) {
          ConsolePutChar(ev.key.ascii);
          linebuffer[linebuffer_last++] = ev.key.ascii;
          ConsoleDrawCursor();
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
