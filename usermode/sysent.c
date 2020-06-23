#include <cpu.h>
#include <trap.h>
#include <string.h>

#include "syscall.h"
#include "proc.h"
#include "dummy_filesystem.h"

extern void vPortDefaultTrapHandler(TrapFrame_t *);

void CloneUserCtx(UserCtx_t *ctx, TrapFrame_t *frame) {
  ctx->pc = (CpuModel > CF_68000) ? frame->m68010.pc : frame->m68000.pc;
  ctx->sp = frame->usp;
  memcpy(&ctx->d0, &frame->d0, 15 * sizeof(uint32_t));
}

static void SysCallOpen(Proc_t *p, TrapFrame_t *frame) {
  int fd = -1;
  for (int i = 0; i < MAXFILES; i++) {
    if (p->fdtab[i] == NULL) {
      fd = i;
      break;
    }
  }
  if (fd != -1) {
    p->fdtab[fd] = Dummy_Open((char *)(frame->d1));
  }
  frame->d0 = (uint32_t)fd;
}

static void SysCallClose(Proc_t *p, TrapFrame_t *frame) {
  p->fdtab[frame->d1]->ops->close(p->fdtab[frame->d1]);
}

static void SysCallRead(Proc_t *p, TrapFrame_t *frame) {
  int fd = (int)(frame->d1);
  char *buf = (char *)(frame->d2);
  size_t nbyte = (size_t)(frame->d3);
  File_t *fp = p->fdtab[fd];
  long res = fp->ops->read(fp, buf, nbyte);
  frame->d0 = res;
}

static void SysCallWrite(Proc_t *p, TrapFrame_t *frame) {
  int fd = (int)(frame->d1);
  char *buf = (char *)(frame->d2);
  size_t nbyte = (size_t)(frame->d3);
  File_t *fp = p->fdtab[fd];
  long res = fp->ops->write(fp, buf, nbyte);
  frame->d0 = res;
}

void vPortTrapHandler(TrapFrame_t *frame) {
  uint16_t sr = (CpuModel > CF_68000) ? frame->m68010.sr : frame->m68000.sr;

  /* Trap instruction from user-space ? */
  if (frame->trapnum == T_TRAPINST && (sr & SR_S) == 0) {
    Proc_t *p = TaskGetProc();

    /* Handle known system calls! */
    if (frame->d0 == SYS_exit) {
      ProcExit(p, frame->d1);
      return;
    }

    if (frame->d0 == SYS_open) {
      SysCallOpen(p, frame);
      return;
    }
    if (frame->d0 == SYS_close) {
      SysCallClose(p, frame);
      return;
    }
    if (frame->d0 == SYS_read) {
      SysCallRead(p, frame);
      return;
    }
    if (frame->d0 == SYS_write) {
      SysCallWrite(p, frame);
      return;
    }
  }

  /* Everything else goes to default trap handler. */
  vPortDefaultTrapHandler(frame);
}
