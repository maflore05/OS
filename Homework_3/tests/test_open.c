#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

typedef struct myfs_file {
    char name[256];
    size_t size;
    time_t atime;
    time_t mtime;
    int is_directory;
    size_t parent_offset;
    size_t next_offset;
    size_t children_offset;
} myfs_file_t;

size_t myfs_traverse_path(void *fsptr, const char *path) {
    char *token, *path_copy;
    size_t current_offset = 0;

    if (path[0] == '/') {
        current_offset = 0;
    }

    path_copy = strdup(path);
    token = strtok(path_copy, "/");

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

int __myfs_open_implem(void *fsptr, size_t fssize, int *errnoptr, const char *path) {
    if (!fsptr || fssize <= 0) {
        if (errnoptr) *errnoptr = EFAULT;
        return -1;
    }

    size_t file_offset = myfs_traverse_path(fsptr, path);
    if (file_offset == (size_t)-1) {
        if (errnoptr) *errnoptr = ENOENT;
        return -1;
    }

    myfs_file_t *file = (myfs_file_t *)((char *)fsptr + file_offset);

    if (strcmp(path, "/") == 0 && file->is_directory) {
        return 0;
    }

    if (file->is_directory) {
        if (errnoptr) *errnoptr = EISDIR;
        return -1;
    }

    return 0;
}

void setup_mock_filesystem(void *fsptr) {
    myfs_file_t *root = (myfs_file_t *)fsptr;

    strcpy(root->name, "/");
    root->size = 0;
    root->is_directory = 1;
    root->parent_offset = 0;
    root->next_offset = 0;
    root->children_offset = sizeof(myfs_file_t);

    myfs_file_t *file1 = (myfs_file_t *)((char *)fsptr + root->children_offset);
    strcpy(file1->name, "file1");
    file1->size = 10;
    file1->is_directory = 0;
    file1->parent_offset = 0;
    file1->next_offset = 0;
    file1->children_offset = 0;
}

int main() {
    size_t fssize = 1024;
    void *fsptr = malloc(fssize);
    memset(fsptr, 0, fssize);

    setup_mock_filesystem(fsptr);

    int err;

    printf("Test 1: Try to open 'file1'\n");
    int result = __myfs_open_implem(fsptr, fssize, &err, "file1");
    if (result == 0) {
        printf("Success: File 'file1' found and accessible\n");
    } else {
        printf("Error: %d\n", err);
    }

    printf("\nTest 2: Try to open a non-existent file 'file2'\n");
    result = __myfs_open_implem(fsptr, fssize, &err, "file2");
    if (result == -1) {
        if (err == ENOENT) {
            printf("Success: File 'file2' does not exist (ENOENT)\n");
        } else {
            printf("Unexpected error: %d\n", err);
        }
    }

    printf("\nTest 3: Try to open the root directory '/'\n");
    result = __myfs_open_implem(fsptr, fssize, &err, "/");
    if (result == 0) {
        printf("Success: Root directory '/' is accessible\n");
    } else {
        printf("Error: %d\n", err);
    }

    printf("\nTest 4: Try to open a non-existent directory '/nonexistent'\n");
    result = __myfs_open_implem(fsptr, fssize, &err, "/nonexistent");
    if (result == -1) {
        if (err == ENOENT) {
            printf("Success: Directory '/nonexistent' does not exist (ENOENT)\n");
        } else {
            printf("Unexpected error: %d\n", err);
        }
    }

    free(fsptr);
    return 0;
}

