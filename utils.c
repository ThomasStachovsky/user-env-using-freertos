#include "utils.h"

char *uint_to_string(unsigned int n, char *buf, int buflen) {
  buf += buflen - 1;
  *buf = '\0';
  do {
    *--buf = '0' + n % 10;
    n /= 10;
  } while (n != 0);

  return buf;
}