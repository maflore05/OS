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
    size_t data_offset;
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

int __myfs_write_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path, const char *buf, size_t size, off_t offset) {
    if (!fsptr) {
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

    if (offset + size > file->size) {
        file->size = offset + size;
        file->data_offset = (char *)realloc((char *)fsptr + file->data_offset, file->size) - (char *)fsptr;
    }

    memcpy((char *)fsptr + file->data_offset + offset, buf, size);
    file->mtime = time(NULL);
    return size;
}

int __myfs_read_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path, char *buf, size_t size, off_t offset) {
    if (!fsptr) {
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

    memcpy(buf, (char *)fsptr + file->data_offset + offset, size);
    return size;
}

void setup_mock_filesystem(void *fsptr) {
    myfs_file_t *root = (myfs_file_t *)fsptr;
    strcpy(root->name, "/");
    root->size = 0;
    root->is_directory = 1;
    root->parent_offset = 0;
    root->next_offset = 0;
    root->children_offset = sizeof(myfs_file_t);
    root->data_offset = 0;

    myfs_file_t *file1 = (myfs_file_t *)((char *)fsptr + root->children_offset);
    strcpy(file1->name, "file1");
    file1->size = 10;
    file1->is_directory = 0;
    file1->parent_offset = 0;
    file1->next_offset = 0;
    file1->children_offset = 0;
    file1->data_offset = sizeof(myfs_file_t) * 2;

    memcpy((char *)fsptr + file1->data_offset, "HelloWorld", file1->size);
}

void test_enoent(void *fsptr) {
    int err;
    int bytes_written = __myfs_write_implem(fsptr, 1024, &err, "nonexistentfile", "test", 4, 0);
    if (bytes_written == -1 && err == ENOENT) {
        printf("Passed\n");
    } else {
        printf("Failed\n");
    }
}

void test_eisdir(void *fsptr) {
    int err;
    int bytes_written = __myfs_write_implem(fsptr, 1024, &err, "/", "test", 4, 0);
    if (bytes_written == -1 && err == EISDIR) {
        printf("Passed\n");
    } else {
        printf("Failed\n");
    }
}

void test_write_read(void *fsptr) {
    int err;

    char *input_data = "NewContent";
    int bytes_written = __myfs_write_implem(fsptr, 1024, &err, "file1", input_data, strlen(input_data), 0);
    if (bytes_written == -1) {
        printf("Write failed with error %d\n", err);
        return;
    }

    char buffer[100];
    int bytes_read = __myfs_read_implem(fsptr, 1024, &err, "file1", buffer, sizeof(buffer), 0);
    if (bytes_read >= 0) {
        buffer[bytes_read] = '\0';
        if (strcmp(buffer, "NewContent") == 0) {
            printf("Passed\n");
        } else {
            printf("Failed: Read content does not match\n");
        }
    } else {
        printf("Read failed with error %d\n", err);
    }
}

int main() {
    void *fsptr = malloc(1024);
    memset(fsptr, 0, 1024);

    setup_mock_filesystem(fsptr);

    test_enoent(fsptr);
    test_eisdir(fsptr);
    test_write_read(fsptr);

    free(fsptr);
    return 0;
}

