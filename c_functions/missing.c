#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"

char *_strcat(char *dest, const char *src)
{
  size_t i, j;
  for (i = 0; dest[i] != '\0'; i++);

  for (j = 0; src[j] != '\0'; j++)
  {
    dest[i + j] = src[j];
  }

  dest[i + j] = '\0';

  return dest;
}
