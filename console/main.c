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

#include "console.h"
#include "event.h"
#include "tty.h"
#include "data/lat2-08.c"
#include "data/pointer.c"

#define mainINPUT_TASK_PRIORITY 3

static COPLIST(cp, 40);

static void vExampleTask(__unused void *data) {
  File_t *tty = TtyOpen();
  char line[80];
  for (;;) {
    FileRead(tty, line, 80);
    if (line[0] == 'c') {
      int n, color;
      char *temp;
      n = strtol(line + 2, &temp, 16);
      color = strtol(temp, NULL, 16);
      CopLoadColor(cp, n, color);
    } else if (line[0] == 'd') {
      char *start, *end, *temp;
      start = (char *)strtol(line + 2, &temp, 16);
      end = (char *)strtol(temp, NULL, 16);
      FileHexDump(tty, start, end - start);
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

static xTaskHandle input_handle;

#include "data/screen.c"

int main(void) {
  // portNOP(); /* Breakpoint for simulator. */

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

  ConsoleInit(&screen_bm, &console_font);

  xTaskCreate(vExampleTask, "input", configMINIMAL_STACK_SIZE, NULL,
              mainINPUT_TASK_PRIORITY, &input_handle);

  vTaskStartScheduler();

  return 0;
}

void vApplicationIdleHook(void) {
}
