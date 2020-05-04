#ifndef _TTY_H_
#define _TTY_H_

#include <file.h>

#define MAX_CANON 80

File_t *TtyOpen(void);
void TtyClose(File_t *f);

#endif
