#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct myfs_file {
    char *name;
    size_t size;
    time_t atime;
    time_t mtime;
    int is_directory;
    struct myfs_file *parent;
    struct myfs_file *next;
    struct myfs_file *children;
} myfs_file_t;

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

int __myfs_open_implem(void *fsptr, size_t fssize, int *errnoptr, const char *path) {
    if (!fsptr || fssize <= 0) {
        if (errnoptr) *errnoptr = EFAULT;
        return -1;
    }

    myfs_file_t *file = myfs_traverse_path((myfs_file_t *)fsptr, path);
    if (!file) {
        if (errnoptr) *errnoptr = ENOENT;
        return -1;
    }

    return 0;
}

void setup_mock_filesystem(myfs_file_t *root) {
    myfs_file_t *file1 = (myfs_file_t *)malloc(sizeof(myfs_file_t));
    file1->name = strdup("file1");
    file1->size = 10;
    file1->is_directory = 0;
    file1->parent = root;
    file1->next = NULL;
    file1->children = NULL;

    root->children = file1;
}

int main() {
    myfs_file_t root = {"/", 0, 0, 0, 1, NULL, NULL, NULL};
    setup_mock_filesystem(&root);

    int err;

    printf("Test 1: Try to open 'file1'\n");
    int result = __myfs_open_implem(&root, 1024, &err, "file1");
    if (result == 0) {
        printf("Success: File 'file1' found and accessible\n");
    } else {
        printf("Error: %d\n", err);
    }

    printf("\nTest 2: Try to open a non-existent file 'file2'\n");
    result = __myfs_open_implem(&root, 1024, &err, "file2");
    if (result == -1) {
        if (err == ENOENT) {
            printf("Success: File 'file2' does not exist (ENOENT)\n");
        } else {
            printf("Unexpected error: %d\n", err);
        }
    }

    printf("\nTest 3: Try to open the root directory '/'\n");
    result = __myfs_open_implem(&root, 1024, &err, "/");
    if (result == 0) {
        printf("Success: Root directory '/' is accessible\n");
    } else {
        printf("Error: %d\n", err);
    }

    printf("\nTest 4: Try to open a non-existent directory '/nonexistent'\n");
    result = __myfs_open_implem(&root, 1024, &err, "/nonexistent");
    if (result == -1) {
        if (err == ENOENT) {
            printf("Success: Directory '/nonexistent' does not exist (ENOENT)\n");
        } else {
            printf("Unexpected error: %d\n", err);
        }
    }

    return 0;
}

