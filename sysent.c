#include <cpu.h>
#include <trap.h>
#include <string.h>
#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <file.h>
#include <strings.h>

#include "syscall.h"
#include "proc.h"
#include "utils.h"
#include "filesys.h"

extern void vPortDefaultTrapHandler(TrapFrame_t *);

void CloneUserCtx(UserCtx_t *ctx, TrapFrame_t *frame) {
  ctx->pc = (CpuModel > CF_68000) ? frame->m68010.pc : frame->m68000.pc;
  ctx->sp = frame->usp;
  memcpy(&ctx->d0, &frame->d0, 15 * sizeof(uint32_t));
}

static void InitForkedProc(void *data) {
  Proc_t *parent_p = (Proc_t *)data;
  Proc_t child_p;

  /* create a new user process with a zero-sized stack */
  ProcInit(&child_p, 0);

  /* get parent's queues */
  child_p.parentreports = parent_p->reports;
  child_p.parentresumenotify = parent_p->resumenotify;

  /* increment the number of parent's children; it doesn't have to be
   * atomic; noone else is using nchildren and parent is suspended */
  parent_p->nchildren++;

  /* child uses parent's stack and executable */
  child_p.ustk = parent_p->ustk;
  child_p.ustksz = parent_p->ustksz;
  child_p.usrctx = parent_p->vfork_usrctx;
  child_p.hunk = parent_p->hunk;

  /* clone file descriptor table and increment .usecount in files */
  memcpy(child_p.fdtab, parent_p->fdtab, MAXFILES * sizeof(File_t *));
  for (int i = 0; i < MAXFILES; i++)
    if (child_p.fdtab[i] != NULL)
      FileHold(child_p.fdtab[i]);

  /* vfork returns 0 in the child */
  child_p.usrctx.d0 = 0;

  TaskSetProc(&child_p);
  ProcEnter(&child_p);

  /* if we are here then it means that there was no successful execv() and then
   * exit() was called */

  printf("Program returned: %d\n", child_p.exitcode); /* useful for debugging */

  /* we cannot call ProcFini(), because we use parent's stack and hunk */
  /* instead we will just close fds and delete our queues */
  for (int i = 0; i < MAXFILES; i++) {
    File_t *f = child_p.fdtab[i];
    if (f != NULL)
      FileClose(f);
  }
  vQueueDelete(child_p.reports);
  vQueueDelete(child_p.resumenotify);

  /* send our pid, resume parent's task and delete current task */
  xQueueSend(child_p.parentresumenotify, &(child_p.pid), portMAX_DELAY);
  vTaskDelete(NULL);
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
    p->fdtab[fd] = FsOpen((char *)(frame->d1));
  }
  if (p->fdtab[fd] == NULL)
    frame->d0 = (uint32_t)-1;
  else
    frame->d0 = (uint32_t)fd;
}

static void SysCallClose(Proc_t *p, TrapFrame_t *frame) {
  if (p->fdtab[frame->d1] == NULL)
    return;
  FileClose(p->fdtab[frame->d1]);
  p->fdtab[frame->d1] = NULL;
}

static void SysCallRead(Proc_t *p, TrapFrame_t *frame) {
  int fd = (int)(frame->d1);
  char *buf = (char *)(frame->d2);
  size_t nbyte = (size_t)(frame->d3);
  File_t *fp = p->fdtab[fd];
  long res = FileRead(fp, buf, nbyte);
  frame->d0 = res;
}

static void SysCallWrite(Proc_t *p, TrapFrame_t *frame) {
  int fd = (int)(frame->d1);
  char *buf = (char *)(frame->d2);
  size_t nbyte = (size_t)(frame->d3);
  File_t *fp = p->fdtab[fd];
  long res = FileWrite(fp, buf, nbyte);
  frame->d0 = res;
}

static void SysCallVfork(Proc_t *p, TrapFrame_t *frame) {

  /* choose a unique task name */
  char name[64];
  char buf[16];
  bzero(name, 64);
  bzero(buf, 16);
  char *spid = uint_to_string((unsigned int)p->pid, buf, 16);
  strcpy(name, "userproc_");
  strcat(name, spid);

  /* the child will return from vfork() and overwrite return address,
   * we keep the return address and restore it after child's execv/exit.
   * XXX: address of return address depends on the structure of vfork() */
  uint32_t retaddr = *(uint32_t *)(frame->usp);

  /* Clone context and create a new freertos task */
  CloneUserCtx(&(p->vfork_usrctx), frame);
  xTaskHandle handle;
  BaseType_t taskres =
    xTaskCreate(InitForkedProc, name, KPROC_STKSZ, p, 0, &handle);
  if (taskres == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) {
    frame->d0 = -1;
    return;
  }

  /* Suspend until child's execv and receive child's pid */
  int child_pid;
  xQueueReceive(p->resumenotify, &child_pid, portMAX_DELAY);

  /* restore return address and set return value */
  *(uint32_t *)(frame->usp) = retaddr;
  frame->d0 = child_pid;
}

static void SysCallExecv(Proc_t *p, TrapFrame_t *frame) {
  const char *path = (const char *)(frame->d1);
  char *const *argv = (char *const *)(frame->d2);

  /* many of the fields in *p remain unchanged:
   * we already have a valid pid
   * we take fdtab from the parent
   * we already created our queues
   * we already initialized nchildren */

  /* keep stack size and address of stack in case of error */
  size_t old_ustksz = p->ustksz;
  void *old_ustk = p->ustk;

  /* initialize the user stack */
  p->ustksz = (UPROC_STKSZ + 3) & -4;
  p->ustk = malloc(p->ustksz);

  if (p->ustk == NULL) {
    p->ustk = old_ustk;
    p->ustksz = old_ustksz;
    frame->d0 = -1;
    return;
  }

  bzero(p->ustk, p->ustksz);

  File_t *executable = FsOpen(path);

  if (executable == NULL) {
    free(p->ustk);
    p->ustk = old_ustk;
    p->ustksz = old_ustksz;
    frame->d0 = -1;
    return;
  }

  if (ProcLoadImage(p, executable)) {
    ProcSetArgv(p, argv);
    xQueueSend(p->parentresumenotify, &(p->pid), portMAX_DELAY);
    ProcEnter(p);
    printf("Program returned: %d\n", p->exitcode); /* useful for debugging */
    ProcFini(p);
    vTaskDelete(NULL);
  } else {
    /* no need to close executable; ProcLoadImage already closed executable. */
    free(p->ustk);
    p->ustk = old_ustk;
    p->ustksz = old_ustksz;
    frame->d0 = -1;
  }
}

static void SysCallWait(Proc_t *p, TrapFrame_t *frame) {
  /* operations on p->nchildren don't have to be atomic */
  if (p->nchildren == 0) {
    frame->d0 = -1;
    return;
  }
  p->nchildren--;
  WaitMessage_t report;
  xQueueReceive(p->reports, &report, portMAX_DELAY);
  frame->d0 = report.pid;
  *(int *)(frame->d1) = report.exitcode;
}

static void SysCallExit(Proc_t *p, TrapFrame_t *frame) {
  WaitMessage_t report = {.pid = p->pid, .exitcode = frame->d1};
  xQueueSend(p->parentreports, &report, portMAX_DELAY);
  ProcExit(p, frame->d1);
}

void vPortTrapHandler(TrapFrame_t *frame) {
  uint16_t sr = (CpuModel > CF_68000) ? frame->m68010.sr : frame->m68000.sr;

  /* Trap instruction from user-space ? */
  if (frame->trapnum == T_TRAPINST && (sr & SR_S) == 0) {
    Proc_t *p = TaskGetProc();

    /* Handle known system calls! */
    switch (frame->d0) {
      case SYS_exit:
        SysCallExit(p, frame);
        return;

      case SYS_open:
        SysCallOpen(p, frame);
        return;

      case SYS_close:
        SysCallClose(p, frame);
        return;

      case SYS_read:
        SysCallRead(p, frame);
        return;

      case SYS_write:
        SysCallWrite(p, frame);
        return;

      case SYS_execv:
        SysCallExecv(p, frame);
        return;

      case SYS_vfork:
        SysCallVfork(p, frame);
        return;

      case SYS_wait:
        SysCallWait(p, frame);
        return;

      default:
        break;
    }
  }

  /* Everything else goes to default trap handler. */
  vPortDefaultTrapHandler(frame);
}
