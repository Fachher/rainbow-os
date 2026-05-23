#ifndef RAMDISK_H
#define RAMDISK_H

#include "include/types.h"

#define RAMDISK_SIZE    (256 * 1024)    /* 256 KB */

void ramdisk_init(void);

#endif
