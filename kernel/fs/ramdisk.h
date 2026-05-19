#ifndef RAMDISK_H
#define RAMDISK_H

#include "include/types.h"

#define RAMDISK_SIZE    (64 * 1024)     /* 64 KB */

void ramdisk_init(void);

#endif
