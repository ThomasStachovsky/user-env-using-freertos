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

typedef struct FsFile {
  File_t f;
  DirEntry_t *de;
} FsFile_t;

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
      FsFile_t *ff;
      void *buf;
      size_t nbyte;
    } read;
  } request;
  struct {
    long *replyp;      /* store result here before task wakeup */
    TaskHandle_t task; /* notify this task when reponse is ready */
  } response;
} FsMsg_t;

static long FsRead(FsFile_t *f, void *buf, size_t nbyte);
static long FsSeek(FsFile_t *f, long offset, int whence);
static void FsClose(FsFile_t *f);

static FileOps_t FsOps = {.read = (FileRead_t)FsRead,
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
static int opened_files;
// static FsFile_t **opened_files;
//#define MAX_NUMBER_ONE_FILE_OPENED 32
//#define BUFFERED_TRACKS_COUNT 10
// static int buffered_tracks_state[BUFFERED_TRACKS_COUNT];
// static void *buffered_tracks[BUFFERED_TRACKS_COUNT];

static void vFileSysTask(__unused void *data) {
  QueueHandle_t replyQ = xQueueCreate(2, sizeof(FloppyIO_t *));
  void *buf = pvPortMalloc(SECTOR_SIZE * SECTOR_COUNT);

  FsMsg_t request;
  short dirsize = 0;
  DirEntry_t *directory = NULL;
  // size_t nfiles = 0;

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
          opened_files = 0;
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
            // nfiles++;
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
          // opened_files = pvPortMalloc(nfiles * sizeof(FsFile_t *));
          // for (int i = 0; i < opened_files; i++) {
          //  opened_files[i] =
          //     pvPortMalloc(MAX_NUMBER_ONE_FILE_OPENED * sizeof(FsFile_t));
        }

        *request.response.replyp = true;
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
      case FS_READ: {
        // XXX: prosta wersja bez buforowania
        long offset = request.request.read.ff->f.offset;
        long start = request.request.read.ff->de->start;
        long size = request.request.read.ff->de->size;
        void *buffer = request.request.read.buf;
        long nbyte = request.request.read.nbyte;

        if (offset >= size || nbyte == 0) {
          *(size_t *)request.response.replyp = 0;
          xTaskNotify(request.response.task, 0, eNoAction);
          break;
        }

        if (nbyte > size - offset)
          nbyte = size - offset;
        long bytes_left = nbyte;
        int track =
          (start * SECTOR_SIZE + offset) / (SECTOR_SIZE * SECTOR_COUNT);
        long start_relative_to_track =
          (start * SECTOR_SIZE + offset) % (SECTOR_SIZE * SECTOR_COUNT);
        long bytes_read_first_track = min(
          bytes_left, (SECTOR_SIZE * SECTOR_COUNT) - start_relative_to_track);
        /*FilePrintf(temptemp,
                   "\nSTART LOCATION:%d\ntrack %d\n start rel %d\n bytes read "
                   "first %d\n",
                   start, track, start_relative_to_track,
                   bytes_read_first_track);*/
        SendIO(&io[0], track++);
        WaitIO(replyQ, buf);

        memcpy(buffer, buf + start_relative_to_track, bytes_read_first_track);
        bytes_left -= bytes_read_first_track;

        while (bytes_left > SECTOR_SIZE * SECTOR_COUNT) {
          SendIO(&io[0], track++);
          WaitIO(replyQ, buf);
          memcpy(buffer, buf, SECTOR_SIZE * SECTOR_COUNT);
          bytes_left -= SECTOR_SIZE * SECTOR_COUNT;
          buffer += SECTOR_SIZE * SECTOR_COUNT;
        }
        SendIO(&io[0], track++);
        WaitIO(replyQ, buf);
        memcpy(buffer, buf, bytes_left);

        request.request.read.ff->f.offset += nbyte;
        *(size_t *)request.response.replyp = nbyte;
        xTaskNotify(request.response.task, 0, eNoAction);
        break;
      }
      default: {
        break;
      }
    }
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
  if (opened_files == 0) {
    // a lot of things
  }
  return opened_files;
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
  DirEntry_t *direntry;
  void *base_p = NULL;
  do {
    direntry = (DirEntry_t *)FsListDir(&base_p);
    if (direntry == NULL)
      return NULL;
    // FilePrintf(temptemp, "STRING: %s\n", direntry->name);
    // FilePrintf(temptemp, "nameparam: %s\n", name);
    // FilePrintf(temptemp, "SIZE: %d\n", direntry->size);
  } while (strcmp(name, direntry->name));
  opened_files++;
  FsFile_t *new_file = pvPortMalloc(sizeof(FsFile_t));
  new_file->de = direntry;
  new_file->f.offset = 0;
  new_file->f.ops = &FsOps;
  new_file->f.usecount = 1;
  // FilePrintf(temptemp, "name %s\noffset %d\nsize %d\n", direntry->name,
  //           ((File_t *)new_file)->offset, direntry->size);
  return &(new_file->f);
}

static void FsClose(FsFile_t *ff) {
  ff->f.usecount--;
  if (ff->f.usecount == 0) {
    vPortFree(ff);
    opened_files--;
  }
}

static long FsRead(FsFile_t *ff, void *buf, size_t nbyte) {
  size_t bytes_read;
  FsMsg_t request = {.cmd = FS_READ,
                     .request.read.ff = ff,
                     .request.read.buf = buf,
                     .request.read.nbyte = nbyte,
                     .response.replyp = (long *)&bytes_read,
                     .response.task = xTaskGetCurrentTaskHandle()};
  (void)xQueueSend(requestQ, &request, portMAX_DELAY);
  (void)xTaskNotifyWait(0xffffffff, 0, NULL, portMAX_DELAY);
  return bytes_read;
}
/* Does not involve direct interaction with the filesystem. */
static long FsSeek(FsFile_t *ff, long offset, int whence) {
  if (ff == NULL)
    return -1;
  else if (whence == SEEK_SET) {
    if (offset < 0)
      return -1;
    else
      ff->f.offset = offset;
  } else if (whence == SEEK_CUR) {
    if (ff->f.offset + offset < 0)
      return -1;
    else
      ff->f.offset += offset;
  } else if (whence == SEEK_END) {
    if ((long)(ff->de->size) + offset < 0)
      return -1;
    else
      ff->f.offset = ff->de->size + offset;
  } else
    return -1;

  return ff->f.offset;
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
