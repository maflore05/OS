#include <stddef.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

/* Helper types and functions */

/* YOUR HELPER FUNCTIONS GO HERE */

#define NAME_MAX_LEN 255
#define BLOCK_SIZE 1024

typedef size_t myfs_off_t;

/* Combined myfs_file_t structure */
typedef struct myfs_file {
    char name[NAME_MAX_LEN];         // Name of the file or directory
    int is_directory;                // 1 if it's a directory, 0 if it's a file
    size_t size;                     // Size of the file (in bytes)
    char *data;                      // Pointer to file data (for files)
    size_t children_offset;          // Offset to child files or directories
    size_t next_offset;              // Offset to next sibling file or directory
    time_t mtime;                    // Last modification time
    time_t ctime;                    // Creation time
    time_t atime;                    // Last access time
} myfs_file_t;

/* Superblock structure */
typedef struct myfs_superblock {
    size_t total_blocks;
    size_t free_blocks;
    size_t block_size;
    size_t namemax;
} myfs_superblock_t;

/* Super structure */
struct myfs_super {
    uint32_t magic;
    myfs_off_t root_dir;
    myfs_off_t free_memory;
    size_t size;
};

/* Node structure (combines files and directories) */

/* Directory structure */
struct myfs_dir {
    size_t number_children;         // Number of children in the directory
    myfs_off_t children;             // Offset to the list of children
};

struct myfs_node {
    char name[NAME_MAX_LEN + 1];    // Name of the node
    char is_file;                   // Indicates if it's a file (1) or directory (0)
    struct timespec times[2];       // Access and modification times
    union {
        struct myfs_file file;      // File data
        struct myfs_dir directory;  // Directory data
    } data;
};

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

/* Helper function to convert offset to pointer */
static void *off_to_ptr(void *fsptr, myfs_off_t offset) {
    return (char *)fsptr + offset;
}

/* Helper function to convert pointer to offset */
static myfs_off_t ptr_to_off(void *fsptr, void *ptr) {
    return (myfs_off_t)((char *)ptr - (char *)fsptr);
}

/* Helper function to find a node by path */
static struct myfs_node *find_node(void *fsptr, const char *path) {
    struct myfs_super *super = (struct myfs_super *)fsptr;
    struct myfs_node *current = off_to_ptr(fsptr, super->root_dir);

    if (strcmp(path, "/") == 0) {
        return current;
    }

    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");

    while (token != NULL) {
        int found = 0;
        myfs_off_t *children = off_to_ptr(fsptr, current->data.directory.children);

        for (size_t i = 0; i < current->data.directory.number_children; i++) {
            struct myfs_node *child = off_to_ptr(fsptr, children[i]);
            if (strcmp(child->name, token) == 0) {
                current = child;
                found = 1;
                break;
            }
        }

        if (!found) {
            free(path_copy);
            return NULL;
        }

        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current;
}

/* Helper function to update modification and access time of a node */
static void update_time(struct myfs_node *node, int set_mod) {
    if (node == NULL) {
        return;
    }

    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        node->times[0] = ts;  // Set access time

        if (set_mod) {
            node->times[1] = ts;  // Set modification time
        }

        node->data.file.atime = ts.tv_sec;  // Set access time in file structure
        if (set_mod) {
            node->data.file.mtime = ts.tv_sec;  // Set modification time in file structure
        }
    }
}

/* Helper function to find parent node of a given path */
static struct myfs_node *find_parent_node(void *fsptr, const char *path, char **last_token) {
    struct myfs_super *super = (struct myfs_super *)fsptr;
    struct myfs_node *current = off_to_ptr(fsptr, super->root_dir);

    if (strcmp(path, "/") == 0) {
        *last_token = strdup("");
        return current;
    }

    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    char *prev_token = NULL;

    while (token != NULL) {
        prev_token = token;
        token = strtok(NULL, "/");

        if (token == NULL) {
            break;
        }

        int found = 0;
        myfs_off_t *children = off_to_ptr(fsptr, current->data.directory.children);

        for (size_t i = 0; i < current->data.directory.number_children; i++) {
            struct myfs_node *child = off_to_ptr(fsptr, children[i]);
            if (strcmp(child->name, token) == 0) {
                current = child;
                found = 1;
                break;
            }
        }

        if (!found) {
            free(path_copy);
            return NULL;
        }
    }

    *last_token = strdup(prev_token);
    free(path_copy);
    return current;
}

/* Helper function to get a node by name from a directory */
static struct myfs_node *get_node(void *fsptr, struct myfs_dir *dir, const char *name) {
    myfs_off_t *children = off_to_ptr(fsptr, dir->children);
    for (size_t i = 0; i < dir->number_children; i++) {
        struct myfs_node *child = off_to_ptr(fsptr, children[i]);
        if (strcmp(child->name, name) == 0) {
            return child;
        }
    }
    return NULL;
}

/* End of helper functions */


/* End of helper functions */

int __myfs_getattr_implem(void *fsptr, size_t fssize, int *errnoptr,
                          uid_t uid, gid_t gid,
                          const char *path, struct stat *stbuf) {
    struct myfs_node *node = find_node(fsptr, path);
    if (node == NULL) {
        *errnoptr = ENOENT;
        return -1;
    }

    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_uid = uid;
    stbuf->st_gid = gid;
    stbuf->st_atime = node->times[0].tv_sec;
    stbuf->st_mtime = node->times[1].tv_sec;
    stbuf->st_ctime = node->times[1].tv_sec;

    if (node->is_file) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = node->data.file.size;
    } else {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_size = 0;
    }

    return 0;
}

int __myfs_readdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, char ***namesptr) {
    struct myfs_node *dir_node = find_node(fsptr, path);
    if (dir_node == NULL || dir_node->is_file) {
        *errnoptr = ENOENT;
        return -1;
    }

    size_t count = dir_node->data.directory.number_children;
    *namesptr = calloc(count, sizeof(char *));
    if (*namesptr == NULL) {
        *errnoptr = ENOMEM;
        return -1;
    }

    myfs_off_t *children = off_to_ptr(fsptr, dir_node->data.directory.children);
    for (size_t i = 0; i < count; i++) {
        struct myfs_node *child = off_to_ptr(fsptr, children[i]);
        (*namesptr)[i] = strdup(child->name);
        if ((*namesptr)[i] == NULL) {
            for (size_t j = 0; j < i; j++) {
                free((*namesptr)[j]);
            }
            free(*namesptr);
            *errnoptr = ENOMEM;
            return -1;
        }
    }

    return count;
}

int __myfs_mknod_implem(void *fsptr, size_t fssize, int *errnoptr, const char *path) {
    struct myfs_super *super = (struct myfs_super *)fsptr;
    struct myfs_super *super_copy = (struct myfs_super *)fsptr;

    // Initialize the file system if not already initialized
    if (super->magic != 0xCAFEBABE) {
        super->magic = 0xCAFEBABE;
        super->size = fssize;
        super->root_dir = sizeof(struct myfs_super);

        struct myfs_node *root = off_to_ptr(fsptr, super->root_dir);
        memset(root->name, '\0', NAME_MAX_LEN + 1);
        strcpy(root->name, "/");
        update_time(root, 1);
        root->is_file = 0;

        root->data.directory.number_children = 0;
        myfs_off_t *children = off_to_ptr(fsptr, super->root_dir + sizeof(struct myfs_node));
        *children = 0;
        root->data.directory.children = ptr_to_off(fsptr, children);

        super->free_memory = ptr_to_off(fsptr, children + 1);
        myfs_off_t *free_memory = off_to_ptr(fsptr, super->free_memory);
        *free_memory = fssize - super->free_memory - sizeof(size_t);
    }

    struct myfs_node *parent_node;
    char *last_token;
    parent_node = find_parent_node(fsptr, path, &last_token);
    if (parent_node == NULL) {
        *errnoptr = ENOENT;
        free(last_token);
        return -1;
    }

    if (parent_node->is_file) {
        *errnoptr = ENOTDIR;
        free(last_token);
        return -1;
    }

    struct myfs_node *existing_node = get_node(fsptr, &parent_node->data.directory, last_token);
    if (existing_node != NULL) {
        *errnoptr = EEXIST;
        free(last_token);
        return -1;
    }

    if (strlen(last_token) > NAME_MAX_LEN) {
        *errnoptr = ENAMETOOLONG;
        free(last_token);
        return -1;
    }

    // Step 1: Validate path
    if (strcmp(path, "/") == 0) {
        *errnoptr = EINVAL; // Cannot create a file named "/"
        return -1;
    }

    // Check if file already exists
    if (find_node(fsptr, path) != NULL) {
        *errnoptr = EEXIST;
        return -1;
    }

    // Step 2: Find parent directory
    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        *errnoptr = ENOMEM;
        return -1;
    }

    char *save_name = strrchr(path_copy, '/');
    save_name++; // Move to the name
    char actual_name[NAME_MAX_LEN];
    strncpy(actual_name, save_name, NAME_MAX_LEN);
    actual_name[NAME_MAX_LEN - 1] = '\0'; // Ensure null termination

    char *file_name = strrchr(path_copy, '/');
    if (file_name == NULL) {
        free(path_copy);
        *errnoptr = EINVAL; // Invalid path
        return -1;
    }
    *file_name = '\0'; // Split path into parent and file
    file_name++;

    struct myfs_node *parent_dir = find_node(fsptr, path_copy);
    free(path_copy);

    if (parent_dir == NULL || parent_dir->is_file) {
        *errnoptr = parent_dir == NULL ? ENOENT : ENOTDIR;
        return -1;
    }

    // Step 3: Check available space
    // struct myfs_super *super = (struct myfs_super *)fsptr;
    size_t required_space = sizeof(struct myfs_node) +
                            (parent_dir->data.directory.number_children + 1) * sizeof(myfs_off_t);

    if (super_copy->free_memory + required_space > super_copy->size) {
        *errnoptr = ENOSPC;
        return -1;
    }

    // Step 4: Allocate and initialize new file node
    struct myfs_node *new_node = off_to_ptr(fsptr, super_copy->free_memory);
    super_copy->free_memory += sizeof(struct myfs_node);

    memset(new_node, 0, sizeof(struct myfs_node));
    strncpy(new_node->name, actual_name, NAME_MAX_LEN);
    new_node->is_file = 1;
    clock_gettime(CLOCK_REALTIME, &new_node->times[0]);
    new_node->times[1] = new_node->times[0];

    // Step 5: Update parent directory children list
    size_t child_array_size = (parent_dir->data.directory.number_children + 1) * sizeof(myfs_off_t);

    // Allocate new block for children
    myfs_off_t new_children_offset = super_copy->free_memory;
    myfs_off_t *new_children = off_to_ptr(fsptr, new_children_offset);

    if (parent_dir->data.directory.number_children > 0) {
        // Copy existing children data to the new block
        myfs_off_t *old_children = off_to_ptr(fsptr, parent_dir->data.directory.children);
        memcpy(new_children, old_children, parent_dir->data.directory.number_children * sizeof(myfs_off_t));
    }

    // Update the children list with the new child
    new_children[parent_dir->data.directory.number_children] = ptr_to_off(fsptr, new_node);
    parent_dir->data.directory.children = new_children_offset;
    parent_dir->data.directory.number_children++;
    super_copy->free_memory += child_array_size;

    return 0;
}

int __myfs_unlink_implem(void *fsptr, size_t fssize, int *errnoptr, const char *path) {
    struct myfs_super *super = (struct myfs_super *)fsptr;
    
    // Find the parent directory and the file name
    char *file_name;
    struct myfs_node *parent_node = find_parent_node(fsptr, path, &file_name);
    
    if (parent_node == NULL) {
        *errnoptr = ENOENT;
        free(file_name);
        return -1;
    }
    
    if (parent_node->is_file) {
        *errnoptr = ENOTDIR;
        free(file_name);
        return -1;
    }
    
    // Find the file node
    struct myfs_node *file_node = get_node(fsptr, &parent_node->data.directory, file_name);
    
    if (file_node == NULL) {
        *errnoptr = ENOENT;
        free(file_name);
        return -1;
    }
    
    if (!file_node->is_file) {
        *errnoptr = EISDIR;
        free(file_name);
        return -1;
    }
    
    // Remove the file from the parent directory
    myfs_off_t *children = off_to_ptr(fsptr, parent_node->data.directory.children);
    size_t num_children = parent_node->data.directory.number_children;
    
    for (size_t i = 0; i < num_children; i++) {
        if (children[i] == ptr_to_off(fsptr, file_node)) {
            // Move the last child to this position and decrease the count
            children[i] = children[num_children - 1];
            parent_node->data.directory.number_children--;
            break;
        }
    }
    
    // Free the file's data blocks
    struct myfs_file *file = &file_node->data.file;
    myfs_off_t current_block = file->data;
    while (current_block != 0) {
        myfs_off_t *next_block = off_to_ptr(fsptr, current_block);
        current_block = *next_block;
        // Mark the block as free (you might want to implement a proper free list)
        *next_block = super->free_memory;
        super->free_memory = ptr_to_off(fsptr, next_block);
    }
    
    // Mark the file node as free
    memset(file_node, 0, sizeof(struct myfs_node));
    *(myfs_off_t *)file_node = super->free_memory;
    super->free_memory = ptr_to_off(fsptr, file_node);
    
    // Update parent directory's modification time
    update_time(parent_node, 1);
    
    free(file_name);
    return 0;
}

/* Implements an emulation of the rmdir system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call deletes the directory indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The function call must fail when the directory indicated by path is
   not empty (if there are files or subdirectories other than . and ..).

   The error codes are documented in man 2 rmdir.

*/
int __myfs_rmdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the mkdir system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call creates the directory indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 mkdir.

*/
int __myfs_mkdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  /* STUB */
  return -1;
}

int __myfs_rename_implem(void *fsptr, size_t fssize, int *errnoptr,
                         const char *from, const char *to) {
    if (!fsptr || !from || !to || from[0] == '\0' || to[0] == '\0') {
        *errnoptr = EINVAL; // Invalid arguments
        return -1;
    }

    // Ensure paths are not identical
    if (strcmp(from, to) == 0) {
        *errnoptr = 0; // No error, nothing to do
        return 0;
    }

    // Locate the file or directory corresponding to `from`
    size_t from_offset = myfs_traverse_path(fsptr, from);
    if (from_offset == (size_t)-1) {
        *errnoptr = ENOENT; // Source not found
        return -1;
    }

    // Ensure `to` does not already exist
    size_t to_offset = myfs_traverse_path(fsptr, to);
    if (to_offset != (size_t)-1) {
        *errnoptr = EEXIST; // Target already exists
        return -1;
    }

    // Retrieve the source file/directory structure
    myfs_file_t *source = (myfs_file_t *)((char *)fsptr + from_offset);

    // Check if it's a non-empty directory
    if (source->is_directory && source->children_offset != 0) {
        *errnoptr = ENOTEMPTY; // Cannot rename non-empty directory
        return -1;
    }

    // Update the name (using strcpy instead of assignment)
    char *new_name = strdup(to + 1); // Extract name from the `to` path
    if (!new_name) {
        *errnoptr = ENOMEM; // Memory allocation failed
        return -1;
    }

    // Free the old name memory and copy the new name into the array
    // Note: the `name` field is a fixed-size array, so it will be overwritten
    strcpy(source->name, new_name); // Copy new name into the name array

    // Free the dynamically allocated memory for the new name
    free(new_name);

    // Re-link the child in the parent directory's linked list (if needed)
    size_t parent_offset = source->children_offset; // This will need to be determined based on your directory structure
    if (parent_offset != 0) {
        myfs_file_t *parent = (myfs_file_t *)((char *)fsptr + parent_offset);
        
        size_t next_offset = parent->children_offset;
        myfs_file_t *child = (next_offset != 0) ? (myfs_file_t *)((char *)fsptr + next_offset) : NULL;

        // Find and update the correct parent-child link in the linked list
        while (child != NULL) {
            if (child == source) {
                break;
            }
            next_offset = child->next_offset;
            child = (next_offset != 0) ? (myfs_file_t *)((char *)fsptr + next_offset) : NULL;
        }
    }

    *errnoptr = 0; // Success
    return 0;
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

    // If the new data size exceeds the current file size, resize the data buffer
    if (offset + size > file->size) {
        file->size = offset + size;

        // Reallocate the 'data' field to hold the new size of data
        file->data = (char *)realloc(file->data, file->size);
        if (file->data == NULL) {
            if (errnoptr) *errnoptr = ENOMEM;
            return -1;
        }
    }

    // Write the data to the file at the specified offset
    memcpy(file->data + offset, buf, size);
    file->mtime = time(NULL); // Update the modification time
    return size;
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

int __myfs_statfs_implem(void *fsptr, size_t fssize, int *errnoptr, struct statvfs *stbuf) {
    if (fsptr == NULL || stbuf == NULL) {
        if (errnoptr != NULL) {
            *errnoptr = EFAULT;
        }
        return -1;
    }

    myfs_superblock_t *fs = (myfs_superblock_t *)fsptr;

    stbuf->f_bsize = fs->block_size;
    stbuf->f_blocks = fs->total_blocks;
    stbuf->f_bfree = fs->free_blocks;
    stbuf->f_bavail = fs->free_blocks;
    stbuf->f_namemax = fs->namemax;

    return 0;
}

