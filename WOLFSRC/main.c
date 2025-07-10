#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "filesystem.h"

int main (int argc, char *argv[]) {
    fs_init(argv);

    PHYSFS_File *f = fs_findFirst("/", ".cmake");
    if (f != NULL) {
        int64_t filesize = PHYSFS_fileLength(f);
        char *buf = malloc(filesize);
        PHYSFS_readBytes(f, buf, filesize);
        printf("Buf:\n%s\n", buf);

        free(buf);
    }

    fs_close();

    return 0;
}
