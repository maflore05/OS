#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#define MAX_NAME_LEN 255
#define MAX_FILE_SIZE 4096

typedef struct myfs_file {
    char name[MAX_NAME_LEN];
    int is_directory;
    size_t size;
    char *data;
    size_t children_offset;
    size_t next_offset;
    time_t mtime;
} myfs_file_t;

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

int __myfs_truncate_implem(void *fsptr, size_t fssize, int *errnoptr,
                           const char *path, off_t offset) {
    if (offset < 0) {
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

    size_t current_size = file->size;

    if ((uintptr_t)fsptr + fssize < (uintptr_t)fsptr + file_offset + offset) {
        *errnoptr = ENOSPC;
        return -1;
    }

    if (offset > current_size) {
        char *new_data = realloc(file->data, offset);
        if (!new_data) {
            *errnoptr = ENOMEM;
            return -1;
        }

        memset(new_data + current_size, 0, offset - current_size);
        file->data = new_data;
    } else if (offset < current_size) {
        char *new_data = realloc(file->data, offset);
        if (!new_data && offset > 0) {
            *errnoptr = ENOMEM;
            return -1;
        }
        file->data = new_data;
    }

    file->size = offset;
    file->mtime = time(NULL);

    *errnoptr = 0;
    return 0;
}

void init_mock_filesystem(myfs_file_t *root) {
    memset(root, 0, sizeof(myfs_file_t));
    strcpy(root->name, "/");
    root->is_directory = 1;

    myfs_file_t *file1 = malloc(sizeof(myfs_file_t));
    memset(file1, 0, sizeof(myfs_file_t));
    strcpy(file1->name, "file1");
    file1->is_directory = 0;
    file1->size = 10;
    file1->data = malloc(10);
    memcpy(file1->data, "abcdefghij", 10);

    myfs_file_t *file2 = malloc(sizeof(myfs_file_t));
    memset(file2, 0, sizeof(myfs_file_t));
    strcpy(file2->name, "file2");
    file2->is_directory = 0;
    file2->size = 10;
    file2->data = malloc(10);
    memcpy(file2->data, "klmnopqrst", 10);

    myfs_file_t *dir1 = malloc(sizeof(myfs_file_t));
    memset(dir1, 0, sizeof(myfs_file_t));
    strcpy(dir1->name, "dir1");
    dir1->is_directory = 1;

    root->children_offset = (size_t)((char *)file1 - (char *)root);
    file1->next_offset = (size_t)((char *)file2 - (char *)file1);
    file2->next_offset = (size_t)((char *)dir1 - (char *)file2);
}

void test_truncate() {
    int err;
    int result;

    void *fsptr = malloc(1024 * 1024);
    size_t fssize = 1024 * 1024;

    init_mock_filesystem((myfs_file_t *)fsptr);

    result = __myfs_truncate_implem(fsptr, fssize, &err, "/file1", 5);
    printf("Test truncate '/file1' to size 5: %s\n", err == 0 ? "Success" : strerror(err));

    result = __myfs_truncate_implem(fsptr, fssize, &err, "/file1", 20);
    printf("Test truncate '/file1' to size 20: %s\n", err == 0 ? "Success" : strerror(err));

    result = __myfs_truncate_implem(fsptr, fssize, &err, "/file1", 5000);
    printf("Test truncate '/file1' to size 5000: %s\n", err == 0 ? "Success" : strerror(err));

    result = __myfs_truncate_implem(fsptr, fssize, &err, "/file2", 50);
    printf("Test truncate '/file2' to size 50: %s\n", err == 0 ? "Success" : strerror(err));

    result = __myfs_truncate_implem(fsptr, fssize, &err, "/dir1", 10);
    printf("Test truncate '/dir1' to size 10: %s\n", err == 0 ? "Success" : strerror(err));
}

int main() {
    test_truncate();
    return 0;
}

