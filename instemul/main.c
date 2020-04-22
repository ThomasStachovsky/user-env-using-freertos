#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

#include <custom.h>
#include <trap.h>

extern int rand(void);

static void vMainTask(__unused void *data) {
  for (;;) {
    custom.color[0] = rand();
  }
}

static xTaskHandle handle;

extern void vPortDefaultTrapHandler(struct TrapFrame *);

void vPortTrapHandler(struct TrapFrame *frame) {
  /* TODO: implement muls.l and divsl.l instruction emulation. */

  /* general version */

  uint64_t instr = ((uint64_t)(*(uint32_t *)(frame->m68000.pc)) << 32) |
                   *(uint32_t *)((frame->m68000.pc) + 4);
  uint32_t instrlow = (uint32_t)(instr & 0xffffffff);
  uint32_t instrhigh = (uint32_t)(instr >> 32);
  /* 0b01001100011111000000100000000000 ; divsl opcode with #<data> mode 111 reg
   * 100 */
  if ((instrhigh & 0b01001100011111000000100000000000) ==
      0b01001100011111000000100000000000) {
    int32_t divisor = *(int32_t *)(&instrlow);
    uint32_t *dq =
      (uint32_t *)((void *)frame +
                   (((instrhigh & 0b0111000000000000) >> 12) << 2));

    uint32_t *dr = (uint32_t *)((void *)frame + (instrhigh << 2));
    int32_t dividend = *(int32_t *)dq;
    int sign_dividend, sign_divisor;
    if (divisor >= 0)
      sign_divisor = 0;
    else {
      divisor = -divisor;
      sign_divisor = 1;
    }

    if (dividend >= 0)
      sign_dividend = 0;
    else {
      dividend = -dividend;
      sign_dividend = 1;
    }
    int32_t q = 0;
    int32_t r = dividend;
    while (r >= divisor) {
      q++;
      r -= divisor;
    }

    if (sign_dividend ^ sign_divisor)
      q = -q;

    if (sign_dividend)
      r -= divisor;

    *dq = q;
    *dr = r;

    frame->m68000.pc += 8;

  }
  /* 00001000000000000100110000111100 ; muls opcode with #<data> mode 111 reg
     100 */
  else if ((instrhigh & 0b01001100001111000000100000000000) ==
           0b01001100001111000000100000000000) {
    int32_t factor = *(int32_t *)(&instrlow);
    uint32_t *destination =
      (uint32_t *)((void *)frame +
                   (((instrhigh & 0b0111000000000000) >> 12) << 2));
    int32_t n;
    int32_t product = *destination;
    int32_t original = product;
    if (factor >= 0)
      n = factor;
    else
      n = -factor;
    for (int i = 0; i < n; i++)
      product += original;

    if (factor >= 0)
      *destination = product;
    else
      *destination = -product;

    frame->m68000.pc += 8;

  } else
    vPortDefaultTrapHandler(frame);

    /* simple version (harmful if used anywhere outside rand.c)*/
#if 0
  {
    uint64_t instr = ((uint64_t)(*(uint32_t *)(frame->m68000.pc)) << 32) |
                     *(uint32_t *)((frame->m68000.pc) + 4);
    frame->m68000.pc += 8;

    if (instr == 0x4c7c28000001f31d) {
      int32_t q = 0, r = frame->d0;
      while (r >= 127773) {
        q++;
        r -= 127773;
      }
      frame->d0 = r;
      frame->d2 = q;

    } else if (instr == 0x4c3c0800000041a7) {
      int32_t p = 0;
      int32_t f = frame->d0;
      for (int i = 0; i < 16807; i++)
        p += f;
      frame->d0 = p;

    } else if (instr == 0x4c3c2800fffff4ec) {
      int32_t p = 0;
      int32_t f = frame->d2;
      for (int i = 0; i < 2836; i++)
        p -= f;
      frame->d2 = p;

    } else
      vPortDefaultTrapHandler(frame);
  }
#endif
}

int main(void) {
  // portNOP(); /* Breakpoint for simulator. */

  xTaskCreate(vMainTask, "main", configMINIMAL_STACK_SIZE, NULL, 0, &handle);

  vTaskStartScheduler();

  return 0;
}

void vApplicationIdleHook(void) {
  custom.color[0] = 0x00f;
}
