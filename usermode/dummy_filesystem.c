#include <FreeRTOS/FreeRTOS.h>
#include "dummy_filesystem.h"

typedef struct DummyFile {
  File_t f;
  char *str;
  int size;
} DummyFile_t;

static long Dummy_Read(DummyFile_t *f, void *buf, size_t nbyte);
static long Dummy_Write(DummyFile_t *f, const void *buf, size_t nbyte);
static void Dummy_Close(DummyFile_t *f);

static FileOps_t DummyOps = {.read = (FileRead_t)Dummy_Read,
                             .write = (FileWrite_t)Dummy_Write,
                             .close = (FileClose_t)Dummy_Close};

/* Close a dummy file */

static void Dummy_Close(DummyFile_t *f) {
  if (f == NULL)
    return;

  if (f->f.usecount > 0)
    f->f.usecount--;

  if (f->f.usecount == 0)
    vPortFree(f);

  return;
}

/* Read from a dummy file */

static long Dummy_Read(DummyFile_t *f, void *buf, size_t nbyte) {
  long to_read = min((long)nbyte, f->size - f->f.offset);
  memcpy(buf, f->str, to_read);
  f->f.offset += to_read;
  return to_read;
}

/* Write to a dummy file */
/* These dummy files are not resizable */

static long Dummy_Write(DummyFile_t *f, const void *buf, size_t nbyte) {
  long to_write = min((long)nbyte, f->size - f->f.offset);
  memcpy(f->str, buf, to_write);
  f->f.offset += to_write;
  return to_write;
}

/* Here is the initialization of the array of dummy files */

#define NFILES 3
#define DEFAULT_STRING_SIZE 100

typedef struct DummyTriple {
  char *name;
  char *str;
  long size;
} DummyTriple_t;

static DummyTriple_t dummy_array[NFILES];
static char name1[DEFAULT_STRING_SIZE], name2[DEFAULT_STRING_SIZE],
  name3[DEFAULT_STRING_SIZE], str1[DEFAULT_STRING_SIZE],
  str2[DEFAULT_STRING_SIZE], str3[DEFAULT_STRING_SIZE];

void Dummy_Init() {

  strcpy(name1, "text1.txt");
  strcpy(str1, "This is a file.\n");

  strcpy(name2, "text2.txt");
  strcpy(str2, "This is another file.\n");

  strcpy(name3, "digits.txt");
  strcpy(str3, "0123456789\n");

  dummy_array[0].name = name1;
  dummy_array[0].str = str1;
  dummy_array[0].size = 16;

  dummy_array[1].name = name2;
  dummy_array[1].str = str2;
  dummy_array[1].size = 22;

  dummy_array[2].name = name3;
  dummy_array[2].str = str3;
  dummy_array[2].size = 11;
}

/* Open a dummy file */

File_t *Dummy_Open(const char *name) {
  for (int i = 0; i < NFILES; i++) {
    if (strcmp(name, dummy_array[i].name) == 0) {
      DummyFile_t *newfile = pvPortMalloc(sizeof(DummyFile_t));
      newfile->size = dummy_array[i].size;
      newfile->str = dummy_array[i].str;
      newfile->f.offset = 0;
      newfile->f.usecount = 1;
      newfile->f.ops = &DummyOps;
      return (File_t *)newfile;
    }
  }
  return NULL;
}