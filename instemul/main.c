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

  /* get instruction */
  uint64_t instr = ((uint64_t)(*(uint32_t *)(frame->m68000.pc)) << 32) |
                   *(uint32_t *)((frame->m68000.pc) + 4);
  uint32_t instrlow = (uint32_t)(instr & 0xffffffff);
  uint32_t instrhigh = (uint32_t)(instr >> 32);

  /* get condition codes register */
  uint8_t *ccr = (((uint8_t *)&(frame->m68000.sr)) + 1);

  /* 00001000000000000100110000111100 ; muls  #<data> mode 111 reg 100 */
  if ((instrhigh & 0b01001100001111000000100000000000) ==
      0b01001100001111000000100000000000) {
    /* get destination register */
    uint32_t *destination =
      (uint32_t *)((void *)frame +
                   (((instrhigh & 0b0111000000000000) >> 12) << 2));

    /* get factors to multiply */
    int32_t factor_one = *(int32_t *)(&instrlow);
    int32_t factor_two = *(int32_t *)destination;

    /* check if overflow */
    if (!((factor_one > 0 &&
           factor_two <= 0b01111111111111111111111111111111 / factor_one &&
           factor_two >= 0b10000000000000000000000000000000 / factor_one) ||
          (factor_one == 0) ||
          (factor_one == -1 &&
           factor_two >= -0b01111111111111111111111111111111) ||
          (factor_one < -1 &&
           factor_two >= 0b01111111111111111111111111111111 / factor_one &&
           factor_two <= 0b10000000000000000000000000000000 / factor_one))) {
      /* V flag */
      *ccr |= 0b00000010;
    } else {
      /* V flag */
      *ccr &= 0b11111101;
    }

    /* do math */
    int32_t product = factor_one * factor_two;

    /* get result in the correct place */
    *destination = product;

    /* N flag */
    if (product < 0)
      *ccr |= 0b00001000;
    else
      *ccr &= 0b11110111;

    /* Z flag */
    if (product == 0)
      *ccr |= 0b00000100;
    else
      *ccr &= 0b11111011;

    /* C flag */
    *ccr &= 0b11111110;

    /* increment pc */
    frame->m68000.pc += 8;
  }
  /* 0b01001100011111000000100000000000 ; divsl #<data> mode 111 reg 100 */
  else if ((instrhigh & 0b01001100011111000000100000000000) ==
           0b01001100011111000000100000000000) {

    /* get divisor */
    int32_t divisor = *(int32_t *)(&instrlow);

    /* check if div by zero */
    if (divisor == 0) {

      /* set trap number to div by zero */
      frame->trapnum = T_ZERODIV;

      /* C flag */
      *ccr &= 0b11111110;

      /* invoke def trap handler */
      vPortDefaultTrapHandler(frame);
      return;
    }

    /* get dividend and quotient destination register */
    uint32_t *dq =
      (uint32_t *)((void *)frame +
                   (((instrhigh & 0b0111000000000000) >> 12) << 2));

    /* get remainder destination register */
    uint32_t *dr = (uint32_t *)((void *)frame + (instrhigh << 2));

    /* dividend is stored in quotient destination register */
    int32_t dividend = *(int32_t *)dq;

    /* check if overflow */
    if (dividend == 0b10000000000000000000000000000000 && divisor == -1) {

      /* V flag */
      *ccr |= 0b00000010;

      /* C flag */
      *ccr &= 0b11111110;

      /* increment pc */
      frame->m68000.pc += 8;
      return;
    }

    /* do math */
    int32_t q, r;
    q = dividend / divisor;
    r = dividend - q * divisor;

    /* get the result in the correct place */
    *dq = q;
    *dr = r;

    /* N flag */
    if (q < 0)
      *ccr |= 0b00001000;
    else
      *ccr &= 0b11110111;

    /* Z flag */
    if (q == 0)
      *ccr |= 0b00000100;
    else
      *ccr &= 0b11111011;

    /* V flag */
    *ccr &= 0b11111101;

    /* C flag */
    *ccr &= 0b11111110;

    /* increment pc */
    frame->m68000.pc += 8;

  } else
    vPortDefaultTrapHandler(frame);
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
