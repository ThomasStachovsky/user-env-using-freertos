#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

#include <interrupt.h>
#include <stdio.h>

#include <copper.h>
#include <palette.h>
#include <bitmap.h>
#include <sprite.h>
#include <font.h>
#include <file.h>
#include <stdlib.h>
#include <serial.h>
#include <custom.h>

#include "console.h"
#include "event.h"
#include "tty.h"
#include "data/lat2-08.c"
#include "data/pointer.c"
#include "data/screen.c"

#include "filesys.h"

#include "proc.h"

#define mainINPUT_TASK_PRIORITY 3

static COPLIST(cp, 40);

static void SystemClockTickHandler(__unused void *data) {
  /* Increment the system timer value and possibly preempt. */
  uint32_t ulSavedInterruptMask = portSET_INTERRUPT_MASK_FROM_ISR();
  xNeedRescheduleTask = xTaskIncrementTick();
  portCLEAR_INTERRUPT_MASK_FROM_ISR(ulSavedInterruptMask);
}

INTSERVER_DEFINE(SystemClockTick, 10, SystemClockTickHandler, NULL);

static void vMainTask(void *tty) {
  FsMount();
  File_t *shell = FsOpen("ushell.exe");

  Proc_t p;
  ProcInit(&p, UPROC_STKSZ);
  TaskSetProc(&p);
  ProcFileInstall(&p, 0, FileHold(tty));
  ProcFileInstall(&p, 1, FileHold(tty));
  if (ProcLoadImage(&p, shell)) {
    ProcSetArgv(&p, (char *[]){"ushell.exe", NULL});
    ProcEnter(&p);
    printf("Program returned: %d\n", p.exitcode);
  }
  ProcFini(&p);

  FileClose(tty);
  vTaskDelete(NULL);
}

static xTaskHandle handle;

int main(void) {
  portNOP(); /* Breakpoint for simulator. */

  /* Configure system clock. */
  AddIntServer(VertBlankChain, SystemClockTick);

  /*
   * Copper configures hardware each frame (50Hz in PAL) to:
   *  - set video mode to HIRES (640x256),
   *  - display one bitplane,
   *  - set background color to black, and foreground to white,
   *  - set up mouse pointer palette,
   *  - set sprite 0 to mouse pointer graphics,
   *  - set other sprites to empty graphics,
   */
  CopSetupScreen(cp, &screen_bm, MODE_HIRES, HP(0), VP(0));
  CopSetupBitplanes(cp, &screen_bm, NULL);
  CopLoadColor(cp, 0, 0x000);
  CopLoadColor(cp, 1, 0xfff);

  /* Set sprite position in upper-left corner of display window. */
  // SpriteUpdatePos(&pointer_spr, HP(0), VP(0));

  /* Tell copper where the copper list begins and enable copper DMA. */
  CopListActivate(cp);

  /* Enable bitplane and sprite fetchers' DMA. */
  EnableDMA(DMAF_RASTER | DMAF_SPRITE);

  ConsoleInit((struct bitmap *)&screen_bm, (struct font *)&console_font);

  File_t *tty = TtyOpen();

  FsInit();

  xTaskCreate(vMainTask, "main", KPROC_STKSZ, tty, 0, &handle);

  vTaskStartScheduler();

  return 0;
}

void vApplicationIdleHook(void) {
}
