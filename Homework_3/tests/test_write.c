#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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

int __myfs_write_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path, const char *buf, size_t size, off_t offset) {
    myfs_file_t *file = myfs_traverse_path((myfs_file_t *)fsptr, path);

    if (!file) {
        if (errnoptr) *errnoptr = ENOENT;
        return -1;
    }

    if (file->is_directory) {
        if (errnoptr) *errnoptr = EISDIR;
        return -1;
    }

    if (offset == 0) {
        offset = file->size;
    }

    if (offset > file->size) {
        if (errnoptr) *errnoptr = EFBIG;
        return -1;
    }

    if (offset + size > file->size) {
        file->size = offset + size;
    }

    if (offset == file->size && size == 0) {
        if (errnoptr) *errnoptr = 0;
        return 0;
    }

    memcpy((char *)fsptr + sizeof(myfs_file_t) + offset, buf, size);

    return size;
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

    char *file_data = "HelloWorld";
    memcpy((char *)root + sizeof(myfs_file_t), file_data, strlen(file_data));
}

int __myfs_read_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path, char *buf, size_t size, off_t offset) {
    myfs_file_t *file = myfs_traverse_path((myfs_file_t *)fsptr, path);

    if (!file) {
        if (errnoptr) *errnoptr = ENOENT;
        return -1;
    }

    if (file->is_directory) {
        if (errnoptr) *errnoptr = EISDIR;
        return -1;
    }

    if (offset >= file->size) {
        if (errnoptr) *errnoptr = 0;
        return 0;
    }

    size_t readable_size = file->size - offset;
    if (size > readable_size) {
        size = readable_size;
    }

    memcpy(buf, (char *)fsptr + sizeof(myfs_file_t) + offset, size);

    return size;
}

int main() {
    myfs_file_t root = {"/", 0, 0, 0, 1, NULL, NULL, NULL};
    setup_mock_filesystem(&root);

    char buffer[100];
    int err;

    int bytes_read = __myfs_read_implem(&root, 1024, &err, "file1", buffer, sizeof(buffer), 0);
    if (bytes_read >= 0) {
        buffer[bytes_read] = '\0';
        printf("Current file contents: %s\n", buffer);
    } else {
        printf("Read error: %d\n", err);
        return -1;
    }

    char user_input[100];
    printf("Enter new data to write into the file: ");
    fgets(user_input, sizeof(user_input), stdin);
    user_input[strcspn(user_input, "\n")] = '\0';

    int bytes_written = __myfs_write_implem(&root, 1024, &err, "file1", user_input, strlen(user_input), 0);
    if (bytes_written >= 0) {
        printf("Bytes written: %d\n", bytes_written);
    } else {
        printf("Write error: %d\n", err);
        return -1;
    }

    bytes_read = __myfs_read_implem(&root, 1024, &err, "file1", buffer, sizeof(buffer), 0);
    if (bytes_read >= 0) {
        buffer[bytes_read] = '\0';
        printf("Updated file contents: %s\n", buffer);
    } else {
        printf("Read error: %d\n", err);
    }

    return 0;
}

