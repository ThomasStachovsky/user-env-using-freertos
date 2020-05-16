#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>
#include <FreeRTOS/queue.h>

#include <floppy.h>
#include <string.h>

#include "filesys.h"

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
      DirEntry_t *direntry;
    } open;
    struct {
      FsFile_t *ff;
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

static QueueHandle_t requestQ;
static QueueHandle_t replyQ;
static int opened_files;
static FloppyIO_t floppyio;
static FsMsg_t curr_msg;
static short dirsize;
static DirEntry_t *directory;

#define DEF_PRIO_BUF_TRACK 0
#define MIN_PRIO_BUF_TRACK (-1000)
#define NUMBER_OF_BUFFERED_TRACKS 10

typedef struct BufferedTrack {
  void *buffer;
  int priority;
  short track;
} BufferedTrack_t;

static BufferedTrack_t buffered_tracks[NUMBER_OF_BUFFERED_TRACKS];

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

static void *GetTrack(short track) {
  int min_prio = buffered_tracks[0].priority;
  int index_min_prio = 0;
  int index_wanted_track = -1;
  for (int i = 0; i < NUMBER_OF_BUFFERED_TRACKS; i++) {
    if (buffered_tracks[i].track == track)
      index_wanted_track = i;
    if (buffered_tracks[i].priority > MIN_PRIO_BUF_TRACK)
      buffered_tracks[i].priority--;
    if (buffered_tracks[i].priority < min_prio) {
      min_prio = buffered_tracks[i].priority;
      index_min_prio = i;
    }
  }
  void *buf;
  if (index_wanted_track != -1) {
    buf = buffered_tracks[index_wanted_track].buffer;
    buffered_tracks[index_wanted_track].priority = DEF_PRIO_BUF_TRACK;
    return buf;
  } else {
    buf = buffered_tracks[index_min_prio].buffer;
    buffered_tracks[index_min_prio].priority = DEF_PRIO_BUF_TRACK;
    buffered_tracks[index_min_prio].track = track;
    SendIO(&floppyio, track);
    WaitIO(replyQ, buf);
    return buf;
  }
}

static void CopyFromFloppyToMemory(int src_track, int src_sector,
                                   int src_offset, void *dst, int nbyte) {
  int track = src_track + (src_sector * SECTOR_SIZE + src_offset) /
                            (SECTOR_SIZE * SECTOR_COUNT);
  int start_relative_to_track =
    (src_sector * SECTOR_SIZE + src_offset) % (SECTOR_SIZE * SECTOR_COUNT);
  long bytes_left = nbyte;
  long bytes_read_curr_track;
  char *track_buf;

  while (bytes_left > 0) {
    bytes_read_curr_track =
      min(bytes_left, (SECTOR_SIZE * SECTOR_COUNT) - start_relative_to_track);
    track_buf = GetTrack(track++);
    memcpy(dst, track_buf + start_relative_to_track, bytes_read_curr_track);
    bytes_left -= bytes_read_curr_track;
    dst += bytes_read_curr_track;
    start_relative_to_track = 0;
  }
}

static void vFileSysCaseMount() {
  if (directory) {
    *curr_msg.response.replyp = false;
  } else {
    for (int i = 0; i < NUMBER_OF_BUFFERED_TRACKS; i++) {
      buffered_tracks[i].priority = DEF_PRIO_BUF_TRACK;
      buffered_tracks[i].track = -1;
    }
    opened_files = 0;
    CopyFromFloppyToMemory(0, 2, 0, &dirsize, sizeof(dirsize));
    directory = (DirEntry_t *)pvPortMalloc(dirsize);
    CopyFromFloppyToMemory(0, 2, sizeof(dirsize), directory, dirsize);
    *curr_msg.response.replyp = true;
  }

  xTaskNotify(curr_msg.response.task, 0, eNoAction);
}

static void vFileSysCaseDirent() {
  char **base_p = (char **)curr_msg.request.dirent.base_p;
  DirEntry_t **replyp = (DirEntry_t **)curr_msg.response.replyp;

  if ((directory == NULL) || (*base_p >= (char *)directory + dirsize)) {
    *replyp = NULL;
  } else if (*base_p == NULL) {
    *replyp = directory;
    *base_p = (char *)directory + directory->reclen;
  } else {
    *replyp = (DirEntry_t *)(*base_p);
    *base_p += ((DirEntry_t *)(*base_p))->reclen;
  }

  xTaskNotify(curr_msg.response.task, 0, eNoAction);
}

static void vFileSysCaseRead() {
  long offset = curr_msg.request.read.ff->f.offset;
  long start = curr_msg.request.read.ff->de->start;
  long size = curr_msg.request.read.ff->de->size;
  void *buffer = curr_msg.request.read.buf;
  long nbyte = curr_msg.request.read.nbyte;

  nbyte = min(nbyte, max(size - offset, 0));

  CopyFromFloppyToMemory(0, start, offset, buffer, nbyte);
  curr_msg.request.read.ff->f.offset += nbyte;
  *(size_t *)curr_msg.response.replyp = nbyte;
  xTaskNotify(curr_msg.response.task, 0, eNoAction);
}

static void vFileSysCaseUnMount() {
  if (opened_files == 0) {
    if (directory != NULL) {
      vPortFree(directory);
      directory = NULL;
    }
  }
  *curr_msg.response.replyp = opened_files;
  xTaskNotify(curr_msg.response.task, 0, eNoAction);
}

static void vFileSysCaseOpen() {
  if (directory == NULL) {
    *(File_t **)curr_msg.response.replyp = NULL;
    xTaskNotify(curr_msg.response.task, 0, eNoAction);
    return;
  }
  opened_files++;
  FsFile_t *new_file = pvPortMalloc(sizeof(FsFile_t));
  new_file->de = curr_msg.request.open.direntry;
  new_file->f.offset = 0;
  new_file->f.ops = &FsOps;
  new_file->f.usecount = 1;
  *(File_t **)curr_msg.response.replyp = &(new_file->f);
  xTaskNotify(curr_msg.response.task, 0, eNoAction);
}

static void vFileSysCaseClose() {
  FsFile_t *ff = curr_msg.request.close.ff;
  ff->f.usecount--;
  if (ff->f.usecount == 0) {
    vPortFree(ff);
    opened_files--;
  }
  xTaskNotify(curr_msg.response.task, 0, eNoAction);
}

static void vFileSysTask(__unused void *data) {

  for (;;) {
    (void)xQueueReceive(requestQ, &curr_msg, portMAX_DELAY);
    switch (curr_msg.cmd) {
      case FS_MOUNT: {
        vFileSysCaseMount();
        break;
      }
      case FS_UNMOUNT: {
        vFileSysCaseUnMount();
        break;
      }
      case FS_DIRENT: {
        vFileSysCaseDirent();
        break;
      }
      case FS_OPEN: {
        vFileSysCaseOpen();
        break;
      }
      case FS_CLOSE: {
        vFileSysCaseClose();
        break;
      }
      case FS_READ: {
        vFileSysCaseRead();
        break;
      }
      default: { break; }
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
  long retval;
  FsMsg_t request = {.cmd = FS_UNMOUNT,
                     .response.replyp = &retval,
                     .response.task = xTaskGetCurrentTaskHandle()};
  (void)xQueueSend(requestQ, &request, portMAX_DELAY);
  (void)xTaskNotifyWait(0xffffffff, 0, NULL, portMAX_DELAY);
  return (int)retval;
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
    if (direntry == NULL) {
      return NULL;
    }
  } while (strcmp(name, direntry->name));
  File_t *f;
  FsMsg_t request = {.cmd = FS_OPEN,
                     .request.open.direntry = direntry,
                     .response.replyp = (long *)&f,
                     .response.task = xTaskGetCurrentTaskHandle()};
  (void)xQueueSend(requestQ, &request, portMAX_DELAY);
  (void)xTaskNotifyWait(0xffffffff, 0, NULL, portMAX_DELAY);
  return f;
}

static void FsClose(FsFile_t *ff) {
  long unused;
  FsMsg_t request = {.cmd = FS_CLOSE,
                     .request.close.ff = ff,
                     .response.replyp = &unused,
                     .response.task = xTaskGetCurrentTaskHandle()};
  (void)xQueueSend(requestQ, &request, portMAX_DELAY);
  (void)xTaskNotifyWait(0xffffffff, 0, NULL, portMAX_DELAY);
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

void FsInit() {
  FloppyInit(FLOPPY_TASK_PRIO);
  requestQ = xQueueCreate(32, sizeof(FsMsg_t));
  replyQ = xQueueCreate(2, sizeof(FloppyIO_t *));
  floppyio.cmd = CMD_READ;
  floppyio.buffer = AllocTrack();
  floppyio.replyQueue = replyQ;
  directory = NULL;
  for (int i = 0; i < NUMBER_OF_BUFFERED_TRACKS; i++) {
    buffered_tracks[i].buffer = pvPortMalloc(SECTOR_SIZE * SECTOR_COUNT);
  }
  xTaskCreate(vFileSysTask, "filesys", configMINIMAL_STACK_SIZE, NULL,
              FILESYS_TASK_PRIO, &filesys_handle);
}
