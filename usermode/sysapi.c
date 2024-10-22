#include "sysapi.h"
#include "syscall.h"

/* Operand classes are described in gcc/config/m68k/m68k.md */
#define SYSCALL1(res, nr, arg1)                                                \
  asm volatile("moveq.l %1,%%d0\n"                                             \
               "move.l  %2,%%d1\n"                                             \
               "trap    #1\n"                                                  \
               "move.l  %%d0,%0\n"                                             \
               : "=rm"(res)                                                    \
               : "i"(nr), "rm"(arg1)                                           \
               : "memory", "cc", "d0", "d1")

#define SYSCALL1_NR(nr, arg1)                                                  \
  asm volatile("moveq.l %0,%%d0\n"                                             \
               "move.l  %1,%%d1\n"                                             \
               "trap    #1\n"                                                  \
               : /* no output */                                               \
               : "i"(nr), "rm"(arg1)                                           \
               : "memory", "cc", "d0", "d1")

#define SYSCALL3(res, nr, arg1, arg2, arg3)                                    \
  asm volatile("moveq.l %1,%%d0\n"                                             \
               "move.l  %2,%%d1\n"                                             \
               "move.l  %3,%%d2\n"                                             \
               "move.l  %4,%%d3\n"                                             \
               "trap    #1\n"                                                  \
               "move.l  %%d0,%0\n"                                             \
               : "=rm"(res)                                                    \
               : "i"(nr), "rm"(arg1), "rm"(arg2), "rm"(arg3)                   \
               : "memory", "cc", "d0", "d1", "d2", "d3")

#define SYSCALL0(res, nr)                                                      \
  asm volatile("moveq.l %1,%%d0\n"                                             \
               "trap    #1\n"                                                  \
               "move.l  %%d0,%0\n"                                             \
               : "=rm"(res)                                                    \
               : "i"(nr)                                                       \
               : "memory", "cc", "d0")

#define SYSCALL2(res, nr, arg1, arg2)                                          \
  asm volatile("moveq.l %1,%%d0\n"                                             \
               "move.l  %2,%%d1\n"                                             \
               "move.l  %3,%%d2\n"                                             \
               "trap    #1\n"                                                  \
               "move.l  %%d0,%0\n"                                             \
               : "=rm"(res)                                                    \
               : "i"(nr), "rm"(arg1), "rm"(arg2)                               \
               : "memory", "cc", "d0", "d1", "d2")

void exit(int status) {
  SYSCALL1_NR(SYS_exit, status);
  for(;;);
}

long read(int fd, void *buf, size_t nbyte) {
  long res;
  SYSCALL3(res, SYS_read, fd, buf, nbyte);
  return res;
}

long write(int fd, const void *buf, size_t nbyte) {
  long res;
  SYSCALL3(res, SYS_write, fd, buf, nbyte);
  return res;
}

int open(const char *path) {
  int fd;
  SYSCALL1(fd, SYS_open, path);
  return fd;
}

void close(int fd) {
  SYSCALL1_NR(SYS_close, fd);
}

int vfork(void) {
  int pid;
  SYSCALL0(pid, SYS_vfork);
  return pid;
}

int execv(const char *path, char *const argv[]) {
  int res;
  SYSCALL2(res, SYS_execv, path, argv);
  return res;
}

int wait(int *statusp) {
  int pid;
  SYSCALL1(pid, SYS_wait, statusp);
  return pid;
}

void sleep(unsigned miliseconds) {
  SYSCALL1_NR(SYS_sleep, miliseconds);
}
