#ifndef FILESYTEM_H
#define FILESYTEM_H

#include <physfs.h>
#include <stdbool.h>

bool fs_init(char *argv[]);
void fs_close(void);

PHYSFS_File *fs_findFirst(const char *dir, const char *pattern);

#endif //FILESYTEM_H
