#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifndef UTIME_NOW
#define UTIME_NOW  ((1L << 30) - 1L)
#endif

#ifndef UTIME_OMIT
#define UTIME_OMIT ((1L << 30) - 2L)
#endif

typedef struct myfs_file {
    char *name;
    size_t size;
    time_t atime;
    time_t mtime;
    time_t ctime;
    int is_directory;
    size_t parent_offset;
    size_t next_offset;
    size_t children_offset;
} myfs_file_t;

size_t myfs_traverse_path(void *fsptr, const char *path);
int __myfs_utimens_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, const struct timespec ts[2]);

size_t myfs_traverse_path(void *fsptr, const char *path) {
    if (!fsptr || !path || path[0] == '\0') {
        return (size_t)-1;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        return (size_t)-1;
    }

    char *token = strtok(path_copy, "/");
    size_t current_offset = 0;

    while (token != NULL) {
        myfs_file_t *current = (myfs_file_t *)((char *)fsptr + current_offset);
        size_t next_offset = current->children_offset;
        myfs_file_t *next = (next_offset != 0) ? (myfs_file_t *)((char *)fsptr + next_offset) : NULL;

        int found = 0;
        while (next != NULL) {
            if (strcmp(next->name, token) == 0) {
                current_offset = next_offset;
                found = 1;
                break;
            }
            next_offset = next->next_offset;
            next = (next_offset != 0) ? (myfs_file_t *)((char *)fsptr + next_offset) : NULL;
        }

        if (!found) {
            free(path_copy);
            return (size_t)-1;
        }

        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current_offset;
}

int __myfs_utimens_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, const struct timespec ts[2]) {
    if (!fsptr || fssize <= 0 || !path || !errnoptr) {
        if (errnoptr) *errnoptr = EFAULT;
        return -1;
    }

    if (strlen(path) == 0 || strcmp(path, "/") == 0) {
        *errnoptr = EINVAL;
        return -1;
    }

    size_t file_offset = myfs_traverse_path(fsptr, path);
    if (file_offset == (size_t)-1) {
        *errnoptr = ENOENT;
        return -1;
    }

    myfs_file_t *file = (myfs_file_t *)((char *)fsptr + file_offset);

    if (file->is_directory) {
        *errnoptr = EISDIR;
        return -1;
    }

    time_t now = time(NULL);
    if (!ts) {
        file->atime = file->mtime = now;
    } else {
        for (int i = 0; i < 2; i++) {
            if (ts[i].tv_nsec != UTIME_NOW && ts[i].tv_nsec != UTIME_OMIT &&
                (ts[i].tv_nsec < 0 || ts[i].tv_nsec >= 1000000000)) {
                *errnoptr = EINVAL;
                return -1;
            }
        }

        if (ts[0].tv_nsec != UTIME_OMIT) {
            file->atime = (ts[0].tv_nsec == UTIME_NOW) ? now : ts[0].tv_sec;
        }
        if (ts[1].tv_nsec != UTIME_OMIT) {
            file->mtime = (ts[1].tv_nsec == UTIME_NOW) ? now : ts[1].tv_sec;
        }
    }

    file->ctime = now;
    return 0;
}

void setup_mock_filesystem(void *fsptr) {
    myfs_file_t *root = (myfs_file_t *)fsptr;

    root->name = strdup("/");
    root->size = 0;
    root->is_directory = 1;
    root->parent_offset = 0;
    root->next_offset = 0;
    root->children_offset = sizeof(myfs_file_t);

    myfs_file_t *file1 = (myfs_file_t *)((char *)fsptr + root->children_offset);
    file1->name = strdup("file1");
    file1->size = 10;
    file1->is_directory = 0;
    file1->parent_offset = 0;
    file1->next_offset = 0;
    file1->children_offset = 0;
}

void test_utimens() {
    size_t fssize = 1024;
    void *fsptr = malloc(fssize);
    memset(fsptr, 0, fssize);

    setup_mock_filesystem(fsptr);

    int err;
    struct timespec ts[2];

    ts[0].tv_sec = time(NULL) - 3600;
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = time(NULL) - 7200;
    ts[1].tv_nsec = 0;

    printf("Test 1: Update 'file1' timestamps\n");
    int result = __myfs_utimens_implem(fsptr, fssize, &err, "file1", ts);
    if (result == 0) {
        printf("Success: Timestamps updated for 'file1'\n");
    } else {
        printf("Error: %d\n", err);
    }

    ts[0].tv_sec = 0;
    ts[0].tv_nsec = UTIME_NOW;
    ts[1].tv_sec = 0;
    ts[1].tv_nsec = UTIME_NOW;

    printf("\nTest 2: Update 'file1' with UTIME_NOW\n");
    result = __myfs_utimens_implem(fsptr, fssize, &err, "file1", ts);
    if (result == 0) {
        printf("Success: Timestamps updated with UTIME_NOW for 'file1'\n");
    } else {
        printf("Error: %d\n", err);
    }

    printf("\nTest 3: Update non-existent file\n");
    result = __myfs_utimens_implem(fsptr, fssize, &err, "nonexistent", ts);
    if (result == -1 && err == ENOENT) {
        printf("Success: Non-existent file handled correctly\n");
    } else {
        printf("Unexpected result: %d, errno: %d\n", result, err);
    }

    free(fsptr);
}

int main() {
    test_utimens();
    return 0;
}

