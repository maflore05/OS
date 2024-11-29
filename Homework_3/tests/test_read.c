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

int __myfs_read_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path, char *buf, size_t size, off_t offset) {
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

    memcpy(buf, (char *)fsptr + file_offset + sizeof(myfs_file_t) + offset, size);
    return size;
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

    char *file_data = "HelloWorld";
    memcpy((char *)fsptr + root->children_offset + sizeof(myfs_file_t), file_data, strlen(file_data));
}

int main() {
    size_t fssize = 1024;
    void *fsptr = malloc(fssize);

    memset(fsptr, 0, fssize);
    setup_mock_filesystem(fsptr);

    char buffer[100];
    int err;

    int bytes_read = __myfs_read_implem(fsptr, fssize, &err, "file1", buffer, sizeof(buffer), 0);
    if (bytes_read >= 0) {
        buffer[bytes_read] = '\0';
        printf("Read first %d bytes: %s\n", bytes_read, buffer);
    } else {
        printf("Error: %d\n", err);
    }

    bytes_read = __myfs_read_implem(fsptr, fssize, &err, "nonexistent_file", buffer, sizeof(buffer), 0);
    if (bytes_read < 0) {
        printf("File not found error: %d\n", err);
    }

    free(fsptr);
    return 0;
}

