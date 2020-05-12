#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>
#include <FreeRTOS/queue.h>

#include <floppy.h>

#include "filesys.h"

// na chwile
static File_t *temptemp;
#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

#include <interrupt.h>
#include <stdio.h>
#include <serial.h>
#include <string.h>

////

typedef enum {
  FS_MOUNT,   /* mount filesystem */
  FS_UNMOUNT, /* unmount filesystem */
  FS_DIRENT,  /* fetch one directory entry */
  FS_OPEN,    /* open a file */
  FS_CLOSE,   /* close the file */
  FS_READ     /* read some bytes from the file */
} FsCmd_t;

/* The type of message send to file system task. */
typedef struct FsMsg {
  FsCmd_t cmd; /* request type */
  union {      /* data specific to given request type */
    struct {
    } mount;
    struct {
    } umount;
    struct {
      DirEntry_t **base_p;
    } dirent;
    struct {
    } open;
    struct {
    } close;
    struct {
    } read;
  } request;
  struct {
    long *replyp;      /* store result here before task wakeup */
    TaskHandle_t task; /* notify this task when reponse is ready */
  } response;
} FsMsg_t;

typedef struct FsFile {
  File_t f;
  DirEntry_t *de;
} FsFile_t;

static long FsRead(FsFile_t *f, void *buf, size_t nbyte);
static long FsSeek(FsFile_t *f, long offset, int whence);
static void FsClose(FsFile_t *f);

__unused static FileOps_t FsOps = {.read = (FileRead_t)FsRead,
                                   .seek = (FileSeek_t)FsSeek,
                                   .close = (FileClose_t)FsClose};

static void SendIO(FloppyIO_t *io, short track) {
  io->track = track;
  FloppySendIO(io);
}

static void WaitIO(QueueHandle_t replyQ, void *buf) {
  FloppyIO_t *io = NULL;
  (void)xQueueReceive(replyQ, &io, portMAX_DELAY);

  DiskSector_t *sectors[SECTOR_COUNT];
  DecodeTrack(io->buffer, sectors);
  for (int j = 0; j < SECTOR_COUNT; j++)
    DecodeSector(sectors[j], buf + j * SECTOR_SIZE);
}

static QueueHandle_t requestQ;

static void vFileSysTask(__unused void *data) {
  QueueHandle_t replyQ = xQueueCreate(2, sizeof(FloppyIO_t *));
  void *buf = pvPortMalloc(SECTOR_SIZE * SECTOR_COUNT);

  FsMsg_t request;
  short dirsize = 0;
  DirEntry_t *directory = NULL;

  /*
   * For double buffering we need (sic!) two track buffers:
   *  - one track will be owned by floppy driver
   *    which will set up a DMA transfer to it
   *  - the track will be decoded from MFM format
   *    and possibly read by this task
   */
  FloppyIO_t io[2];
  for (short i = 0; i < 2; i++) {
    io[i].cmd = CMD_READ;
    io[i].buffer = AllocTrack();
    io[i].replyQueue = replyQ;
  }
  char **base_p;
  for (;;) {
    // portNOP();
    (void)xQueueReceive(requestQ, &request, portMAX_DELAY);
    // portNOP();
    switch (request.cmd) {
      case FS_MOUNT: {
        if (directory) {
          *request.response.replyp = false;
          xTaskNotify(request.response.task, 0, eNoAction);
        } else {
          int track = 0;
          SendIO(&io[0], track++);
          WaitIO(replyQ, buf);
          dirsize = *(short *)((char *)buf + SECTOR_SIZE + SECTOR_SIZE);
          short bytesleft = dirsize;
          short offset = 0;
          directory = (DirEntry_t *)pvPortMalloc(dirsize);
          char *ptr = (char *)buf + SECTOR_SIZE + SECTOR_SIZE + 2;
          // int first = 1;
          while (bytesleft > 0) {
            memcpy((char *)directory + offset, ptr,
                   ((DirEntry_t *)ptr)->reclen);
            // FilePrintf(temptemp, "Loaded %s\n", directory + offset)
            FilePrintf(temptemp, "bytesleft: %d ; Loaded %s\n", bytesleft,
                       ((DirEntry_t *)((char *)directory + offset))->name);
            offset += ((DirEntry_t *)ptr)->reclen;
            bytesleft -= ((DirEntry_t *)ptr)->reclen;
            ptr = (char *)ptr + ((DirEntry_t *)ptr)->reclen;
            // FilePrintf(temptemp, "XXX %d XXX\n",
            //           ptr - (char *)buf -
            //             first * (SECTOR_SIZE + SECTOR_SIZE - 2));
            if (ptr - (char *)buf >= SECTOR_COUNT * SECTOR_SIZE) {
              // first = 0;
              SendIO(&io[0], track++);
              WaitIO(replyQ, buf);
              ptr = (char *)buf;
            }
          }
          *request.response.replyp = true;
        }
        xTaskNotify(request.response.task, 0, eNoAction);
        break;
      }
      case FS_DIRENT: {
        base_p = (char **)request.request.dirent.base_p;
        if (*base_p >= (char *)directory + dirsize) {
          *(DirEntry_t **)request.response.replyp = (DirEntry_t *)NULL;
        } else if (*base_p == NULL) {
          *(DirEntry_t **)request.response.replyp = (DirEntry_t *)directory;
          *base_p = (char *)directory + directory->reclen;
        } else {
          *(DirEntry_t **)request.response.replyp = (DirEntry_t *)(*base_p);
          *base_p = (char *)(*base_p) + ((DirEntry_t *)(*base_p))->reclen;
        }
        xTaskNotify(request.response.task, 0, eNoAction);
        break;
      }
      default: {
        break;
      }
    }

#if 0

    short track = 0;
    short active = 0;

    /* Initiate double buffered reads. */
    SendIO(&io[active], track++);
    active ^= 1;
    short totalsize;
    int first = 1;
    do {
      /* Request asynchronous read into second buffer. */
      SendIO(&io[active], track++);
      active ^= 1;
      /* Wait for reply with first buffer and decode it. */
      WaitIO(replyQ, buf);
      if (first) {
        totalsize = *(short *)((char *)buf + SECTOR_SIZE + SECTOR_SIZE);
        first = 0;
      }
      if (totalsize > 0) {
        DirEntry_t *ptr =
          (DirEntry_t *)((char *)buf + SECTOR_SIZE + SECTOR_SIZE + 2);
        totalsize -= ptr->reclen;
        FilePrintf(temptemp, "Name: %s Beg: %d Len: %d Type: %d \n", ptr->name,
                   ptr->start, ptr->size, ptr->type);
        FilePrintf(temptemp, "Preview:\n");
        for (int i = 0; i < 300; i++)
          FilePutChar(temptemp, ((char *)buf)[ptr->start * SECTOR_SIZE + i]);
        FilePrintf(temptemp, "\n");
        while (totalsize > 0) {
          ptr = (DirEntry_t *)((char *)ptr + ptr->reclen);
          totalsize -= ptr->reclen;
          FilePrintf(temptemp, "Name: %s Beg: %d Len: %d Type: %d \n",
                     ptr->name, ptr->start, ptr->size, ptr->type);
          FilePrintf(temptemp, "Preview:\n");
          for (int i = 0; i < 300; i++)
            FilePutChar(temptemp, ((char *)buf)[ptr->start * SECTOR_SIZE + i]);
          FilePrintf(temptemp, "\n");
        }
      }

    } while (track < TRACK_COUNT);

    /* Finish last track. */
    WaitIO(replyQ, buf);

    /* Wait two seconds and repeat. */
    vTaskDelay(2000 / portTICK_PERIOD_MS);

#endif
  }
}

bool FsMount(void) {
  long retval;
  FsMsg_t request = {.cmd = FS_MOUNT,
                     .response.replyp = &retval,
                     .response.task = xTaskGetCurrentTaskHandle()};
  (void)xQueueSend(requestQ, &request, portMAX_DELAY);
  (void)xTaskNotifyWait(0xffffffff, 0, NULL, portMAX_DELAY);

  return retval;
}

/* Remember to free memory used up by a directory! */
int FsUnMount(void) {
  return 0;
}

const DirEntry_t *FsListDir(void **base_p) {
  DirEntry_t *ptr;
  FsMsg_t request = {.cmd = FS_DIRENT,
                     .request.dirent.base_p = (DirEntry_t **)base_p,
                     .response.replyp = (long *)&ptr,
                     .response.task = xTaskGetCurrentTaskHandle()};
  (void)xQueueSend(requestQ, &request, portMAX_DELAY);
  (void)xTaskNotifyWait(0xffffffff, 0, NULL, portMAX_DELAY);
  return ptr;
}

File_t *FsOpen(const char *name) {
  (void)name;
  return NULL;
}

static void FsClose(FsFile_t *ff) {
  (void)ff;
}

static long FsRead(FsFile_t *ff, void *buf, size_t nbyte) {
  (void)ff;
  (void)buf;
  (void)nbyte;
  return -1;
}

/* Does not involve direct interaction with the filesystem. */
static long FsSeek(FsFile_t *ff, long offset, int whence) {
  (void)ff;
  (void)offset;
  (void)whence;
  return -1;
}

static TaskHandle_t filesys_handle;

#define FLOPPY_TASK_PRIO 3
#define FILESYS_TASK_PRIO 2

void FsInit(File_t *ptr) {
  temptemp = ptr;
  FloppyInit(FLOPPY_TASK_PRIO);
  requestQ = xQueueCreate(32, sizeof(FsMsg_t));
  xTaskCreate(vFileSysTask, "filesys", configMINIMAL_STACK_SIZE, NULL,
              FILESYS_TASK_PRIO, &filesys_handle);
}
