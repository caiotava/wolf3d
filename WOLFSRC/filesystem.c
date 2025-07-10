#include "filesystem.h"
#include <SDL2/SDL.h>

bool fs_init(char *argv[]) {
    if (!PHYSFS_init(argv[0])) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "error to initialize physfs %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    PHYSFS_mount(".", NULL, 1);

    return true;
}

void fs_close(void) {
    PHYSFS_deinit();
}

PHYSFS_File *fs_findFirst(const char *dir, const char *pattern) {
    char **rc = PHYSFS_enumerateFiles(dir);
    char **i;

    for (i = rc; *i != NULL; i++) {
        if (strstr(*i, pattern) != NULL) {
            break;
        }
    }

    PHYSFS_File *f = NULL;
    if (*i != NULL) {
        f = PHYSFS_openRead(*i);
    }

    PHYSFS_freeList(rc);

    return f;
}