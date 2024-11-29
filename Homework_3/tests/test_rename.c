#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_NAME_LEN 256
#define EEXIST 17
#define ENOENT 2
#define ENOTEMPTY 39

typedef struct myfs_file {
    char *name;
    size_t size;
    int is_directory;
    size_t parent_offset;
    size_t next_offset;
    size_t children_offset;
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

void setup_mock_filesystem(void *fsptr) {
    myfs_file_t *root = (myfs_file_t *)fsptr;

    myfs_file_t *file1 = (myfs_file_t *)((char *)fsptr + sizeof(myfs_file_t));
    file1->name = strdup("file1");
    file1->size = 10;
    file1->is_directory = 0;
    file1->parent_offset = 0;
    file1->next_offset = 0;
    file1->children_offset = 0;

    myfs_file_t *dir1 = (myfs_file_t *)((char *)fsptr + sizeof(myfs_file_t) + sizeof(myfs_file_t));
    dir1->name = strdup("dir1");
    dir1->size = 0;
    dir1->is_directory = 1;
    dir1->parent_offset = 0;
    dir1->next_offset = 0;
    dir1->children_offset = 0;

    root->children_offset = sizeof(myfs_file_t);
    file1->next_offset = sizeof(myfs_file_t) + sizeof(myfs_file_t);
    dir1->next_offset = 0;
}

int __myfs_rename_implem(void *fsptr, size_t fssize, int *err, const char *old_path, const char *new_path) {
    if (!fsptr || !old_path || !new_path || old_path[0] == '\0' || new_path[0] == '\0') {
        *err = EINVAL;
        return -1;
    }

    size_t old_offset = myfs_traverse_path(fsptr, old_path);
    if (old_offset == (size_t)-1) {
        *err = ENOENT;
        return -1;
    }

    size_t new_offset = myfs_traverse_path(fsptr, new_path);
    if (new_offset != (size_t)-1) {
        *err = EEXIST;
        return -1;
    }

    myfs_file_t *file = (myfs_file_t *)((char *)fsptr + old_offset);
    if (file->is_directory && file->children_offset != 0) {
        *err = ENOTEMPTY;
        return -1;
    }

    free(file->name);
    file->name = strdup(new_path + 1);

    myfs_file_t *parent = (myfs_file_t *)((char *)fsptr + file->parent_offset);
    size_t next_offset = parent->children_offset;
    myfs_file_t *child = (next_offset != 0) ? (myfs_file_t *)((char *)fsptr + next_offset) : NULL;

    while (child != NULL) {
        if (child == file) {
            break;
        }
        next_offset = child->next_offset;
        child = (next_offset != 0) ? (myfs_file_t *)((char *)fsptr + next_offset) : NULL;
    }

    return 0;
}

void test_rename(void *fsptr) {
    int err;

    if (__myfs_rename_implem(fsptr, 1024, &err, "/nonexistent", "/newfile") == -1 && err == ENOENT) {
        printf("Test ENOENT: Passed\n");
    } else {
        printf("Test ENOENT: Failed\n");
    }

    if (__myfs_rename_implem(fsptr, 1024, &err, "/file1", "/file1") == -1 && err == EEXIST) {
        printf("Test EEXIST: Passed\n");
    } else {
        printf("Test EEXIST: Failed\n");
    }

    if (__myfs_rename_implem(fsptr, 1024, &err, "/file1", "/newfile1") == 0) {
        myfs_file_t *file1_after = (myfs_file_t *)((char *)fsptr + myfs_traverse_path(fsptr, "/newfile1"));
        if (file1_after != NULL && strcmp(file1_after->name, "newfile1") == 0) {
            printf("Test successful rename: Passed\n");
        } else {
            printf("Test successful rename: Failed\n");
        }
    } else {
        printf("Test successful rename: Failed\n");
    }

    if (__myfs_rename_implem(fsptr, 1024, &err, "/dir1", "/newdir1") == -1 && err == ENOTEMPTY) {
        printf("Test directory rename (ENOTEMPTY): Passed\n");
    } else {
        printf("Test directory rename (ENOTEMPTY): Failed\n");
    }

    if (__myfs_rename_implem(fsptr, 1024, &err, "/dir1", "/newdir1") == 0) {
        myfs_file_t *dir1_after = (myfs_file_t *)((char *)fsptr + myfs_traverse_path(fsptr, "/newdir1"));
        if (dir1_after != NULL && dir1_after->parent_offset == 0) {
            printf("Test directory move: Passed\n");
        } else {
            printf("Test directory move: Failed\n");
        }
    } else {
        printf("Test directory move: Failed\n");
    }
}

int main() {
    void *fsptr = malloc(1024);
    setup_mock_filesystem(fsptr);
    test_rename(fsptr);
    free(fsptr);
    return 0;
}

