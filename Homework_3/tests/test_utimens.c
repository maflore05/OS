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
    struct myfs_file *parent;
    struct myfs_file *next;
    struct myfs_file *children;
} myfs_file_t;

myfs_file_t *myfs_traverse_path(myfs_file_t *root, const char *path);
int __myfs_utimens_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, const struct timespec ts[2]);

myfs_file_t *myfs_traverse_path(myfs_file_t *root, const char *path) {
    char *token, *path_copy;
    myfs_file_t *current = root;

    if (path[0] == '/') {
        current = root;
    }

    path_copy = strdup(path);
    token = strtok(path_copy, "/");

    while (token != NULL) {
        myfs_file_t *next = current->children;
        while (next != NULL) {
            if (strcmp(next->name, token) == 0) {
                current = next;
                break;
            }
            next = next->next;
        }

        if (next == NULL) {
            free(path_copy);
            return NULL;
        }

        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current;
}

int __myfs_utimens_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, const struct timespec ts[2]) {
    if (!fsptr || !path || !errnoptr) {
        *errnoptr = EFAULT;
        return -1;
    }

    if (strlen(path) == 0) {
        *errnoptr = ENOENT;
        return -1;
    }

    myfs_file_t *file = myfs_traverse_path((myfs_file_t *)fsptr, path);
    if (!file) {
        *errnoptr = ENOENT;
        return -1;
    }

    time_t now = time(NULL);
    if (ts) {
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
    } else {
        file->atime = file->mtime = now;
    }

    file->ctime = now;
    return 0;
}

void setup_mock_filesystem(myfs_file_t *root) {
    myfs_file_t *file1 = (myfs_file_t *)malloc(sizeof(myfs_file_t));
    file1->name = strdup("file1");
    file1->size = 10;
    file1->is_directory = 0;
    file1->atime = file1->mtime = file1->ctime = time(NULL);
    file1->parent = root;
    file1->next = NULL;
    file1->children = NULL;

    root->children = file1;
}

void test_utimens() {
    myfs_file_t root = {"/", 0, time(NULL), time(NULL), time(NULL), 1, NULL, NULL, NULL};
    setup_mock_filesystem(&root);

    int err;
    struct timespec ts[2];

    ts[0].tv_sec = time(NULL) - 3600;
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = time(NULL) - 7200;
    ts[1].tv_nsec = 0;

    printf("Test 1: Update 'file1' timestamps\n");
    int result = __myfs_utimens_implem(&root, 1024, &err, "file1", ts);
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
    result = __myfs_utimens_implem(&root, 1024, &err, "file1", ts);
    if (result == 0) {
        printf("Success: Timestamps updated with UTIME_NOW for 'file1'\n");
    } else {
        printf("Error: %d\n", err);
    }

    printf("\nTest 3: Update non-existent file\n");
    result = __myfs_utimens_implem(&root, 1024, &err, "nonexistent", ts);
    if (result == -1 && err == ENOENT) {
        printf("Success: Non-existent file handled correctly\n");
    } else {
        printf("Unexpected result: %d, errno: %d\n", result, err);
    }
}

int main() {
    test_utimens();
    return 0;
}

