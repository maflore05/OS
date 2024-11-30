/*

  MyFS: a tiny file-system written for educational purposes

  MyFS is 

  Copyright 2018-21 by

  University of Alaska Anchorage, College of Engineering.

  Copyright 2022-24

  University of Texas at El Paso, Department of Computer Science.

  Contributors: Christoph Lauter 
                ... and
                ...

  and based on 

  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall myfs.c implementation.c `pkg-config fuse --cflags --libs` -o myfs

*/

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


/* The filesystem you implement must support all the 13 operations
   stubbed out below. There need not be support for access rights,
   links, symbolic links. There needs to be support for access and
   modification times and information for statfs.

   The filesystem must run in memory, using the memory of size 
   fssize pointed to by fsptr. The memory comes from mmap and 
   is backed with a file if a backup-file is indicated. When
   the filesystem is unmounted, the memory is written back to 
   that backup-file. When the filesystem is mounted again from
   the backup-file, the same memory appears at the newly mapped
   in virtual address. The filesystem datastructures hence must not
   store any pointer directly to the memory pointed to by fsptr; it
   must rather store offsets from the beginning of the memory region.

   When a filesystem is mounted for the first time, the whole memory
   region of size fssize pointed to by fsptr reads as zero-bytes. When
   a backup-file is used and the filesystem is mounted again, certain
   parts of the memory, which have previously been written, may read
   as non-zero bytes. The size of the memory region is at least 2048
   bytes.

   CAUTION:

   * You MUST NOT use any global variables in your program for reasons
   due to the way FUSE is designed.

   You can find ways to store a structure containing all "global" data
   at the start of the memory region representing the filesystem.

   * You MUST NOT store (the value of) pointers into the memory region
   that represents the filesystem. Pointers are virtual memory
   addresses and these addresses are ephemeral. Everything will seem
   okay UNTIL you remount the filesystem again.

   You may store offsets/indices (of type size_t) into the
   filesystem. These offsets/indices are like pointers: instead of
   storing the pointer, you store how far it is away from the start of
   the memory region. You may want to define a type for your offsets
   and to write two functions that can convert from pointers to
   offsets and vice versa.

   * You may use any function out of libc for your filesystem,
   including (but not limited to) malloc, calloc, free, strdup,
   strlen, strncpy, strchr, strrchr, memset, memcpy. However, your
   filesystem MUST NOT depend on memory outside of the filesystem
   memory region. Only this part of the virtual memory address space
   gets saved into the backup-file. As a matter of course, your FUSE
   process, which implements the filesystem, MUST NOT leak memory: be
   careful in particular not to leak tiny amounts of memory that
   accumulate over time. In a working setup, a FUSE process is
   supposed to run for a long time!

   * And of course: your code MUST NOT SEGFAULT!

   It is reasonable to proceed in the following order:

   (1)   Design and implement a mechanism that initializes a filesystem
         whenever the memory space is fresh. That mechanism can be
         implemented in the form of a filesystem handle into which the
         filesystem raw memory pointer and sizes are translated.
         Check that the filesystem does not get reinitialized at mount
         time if you initialized it once and unmounted it but that all
         pieces of information (in the handle) get read back correctly
         from the backup-file. 

   (2)   Design and implement functions to find and allocate free memory
         regions inside the filesystem memory space. There need to be 
         functions to free these regions again, too. Any "global" variable
         goes into the handle structure the mechanism designed at step (1) 
         provides.

   (3)   Carefully design a data structure able to represent all the
         pieces of information that are needed for files and
         (sub-)directories.  You need to store the location of the
         root directory in a "global" variable that, again, goes into the 
         handle designed at step (1).
          
   (4)   Write __myfs_getattr_implem and debug it thoroughly, as best as
         you can with a filesystem that is reduced to one
         function. Writing this function will make you write helper
         functions to traverse paths, following the appropriate
         subdirectories inside the file system. Strive for modularity for
         these filesystem traversal functions.

   (5)   Design and implement __myfs_readdir_implem. You cannot test it
         besides by listing your root directory with ls -la and looking
         at the date of last access/modification of the directory (.). 
         Be sure to understand the signature of that function and use
         caution not to provoke segfaults nor to leak memory.

   (6)   Design and implement __myfs_mknod_implem. You can now touch files 
         with 

         touch foo

         and check that they start to exist (with the appropriate
         access/modification times) with ls -la.

*/

/* Helper types and functions */

#define NAME_MAX_LEN 255

typedef size_t myfs_off_t;

struct myfs_super {
    uint32_t is_set;
    myfs_off_t root_dir;
    myfs_off_t free_memory;
    size_t size;
};

struct myfs_file {
    size_t size;
    size_t allocated;
    myfs_off_t data;
    myfs_off_t next_file_block;
};

struct myfs_dir {
    size_t number_children;
    myfs_off_t children;
};

struct myfs_node {
    char name[NAME_MAX_LEN + 1];
    char is_file; // 0 is directory, 1 is file
    struct timespec times[2];
    union {
        struct myfs_file file;
        struct myfs_dir directory;
    } data;
};

static void update_time(struct myfs_node *node, int set_mod) {
    if (node == NULL) {
        return;
    }

    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        node->times[0] = ts;
        if (set_mod) {
            node->times[1] = ts;
        }
    }
}

static void *off_to_ptr(void *fsptr, myfs_off_t offset) {
    return (char *)fsptr + offset;
}

static myfs_off_t ptr_to_off(void *fsptr, void *ptr) {
    return (myfs_off_t)((char *)ptr - (char *)fsptr);
}

struct myfs_super *initialize_myfs(void *fsptr, size_t fssize) {
    struct myfs_super *super = (struct myfs_super *)fsptr;

    // Check if the filesystem is already initialized
    if (super->is_set != 1) {
        super->is_set = 1;
        super->size = fssize;
        super->root_dir = sizeof(struct myfs_super);

        // Initialize the root directory node
        struct myfs_node *root = off_to_ptr(fsptr, super->root_dir);
        memset(root->name, '\0', NAME_MAX_LEN + 1);
        strcpy(root->name, "/");
        update_time(root, 1);  // Mark the root directory creation time
        root->is_file = 0;    // Mark as a directory

        // Initialize root directory's children
        root->data.directory.number_children = 0;
        myfs_off_t *children = off_to_ptr(fsptr, super->root_dir + sizeof(struct myfs_node));
        *children = 0;
        root->data.directory.children = ptr_to_off(fsptr, children);

        // Set up free memory pointer
        super->free_memory = ptr_to_off(fsptr, children + 1);
        myfs_off_t *free_memory = off_to_ptr(fsptr, super->free_memory);
        *free_memory = fssize - super->free_memory - sizeof(size_t);
    }

    return super;
}

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

static struct myfs_node *find_parent_node(void *fsptr, const char *path, char **last_token) {
    struct myfs_super *super = (struct myfs_super *)fsptr;
    struct myfs_node *current = off_to_ptr(fsptr, super->root_dir);

    if (strcmp(path, "/") == 0) {
        *last_token = strdup("");
        return current;
    }

    // Normalize path
    char *path_copy = strdup(path);
    if (!path_copy) return NULL;
    while (path_copy[strlen(path_copy) - 1] == '/')
        path_copy[strlen(path_copy) - 1] = '\0'; // Remove trailing slashes

    char *token = strtok(path_copy, "/");
    char *prev_token = NULL;

    while (token != NULL) {
        prev_token = token;
        char *next_token = strtok(NULL, "/");

        if (!next_token) {
            // Stop before the last token to find the parent
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
            return NULL; // Child not found
        }

        token = next_token;
    }

    *last_token = strdup(prev_token);
    free(path_copy);
    return current;
}

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

/* Implements an emulation of the stat system call on the filesystem 
   of size fssize pointed to by fsptr. 
   
   If path can be followed and describes a file or directory 
   that exists and is accessable, the access information is 
   put into stbuf. 

   On success, 0 is returned. On failure, -1 is returned and 
   the appropriate error code is put into *errnoptr.

   man 2 stat documents all possible error codes and gives more detail
   on what fields of stbuf need to be filled in. Essentially, only the
   following fields need to be supported:

   st_uid      the value passed in argument
   st_gid      the value passed in argument
   st_mode     (as fixed values S_IFDIR | 0755 for directories,
                                S_IFREG | 0755 for files)
   st_nlink    (as many as there are subdirectories (not files) for directories
                (including . and ..),
                1 for files)
   st_size     (supported only for files, where it is the real file size)
   st_atim
   st_mtim

*/
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

/* Implements an emulation of the readdir system call on the filesystem 
   of size fssize pointed to by fsptr. 

   If path can be followed and describes a directory that exists and
   is accessable, the names of the subdirectories and files 
   contained in that directory are output into *namesptr. The . and ..
   directories must not be included in that listing.

   If it needs to output file and subdirectory names, the function
   starts by allocating (with calloc) an array of pointers to
   characters of the right size (n entries for n names). Sets
   *namesptr to that pointer. It then goes over all entries
   in that array and allocates, for each of them an array of
   characters of the right size (to hold the i-th name, together 
   with the appropriate '\0' terminator). It puts the pointer
   into that i-th array entry and fills the allocated array
   of characters with the appropriate name. The calling function
   will call free on each of the entries of *namesptr and 
   on *namesptr.

   The function returns the number of names that have been 
   put into namesptr. 

   If no name needs to be reported because the directory does
   not contain any file or subdirectory besides . and .., 0 is 
   returned and no allocation takes place.

   On failure, -1 is returned and the *errnoptr is set to 
   the appropriate error code. 

   The error codes are documented in man 2 readdir.

   In the case memory allocation with malloc/calloc fails, failure is
   indicated by returning -1 and setting *errnoptr to EINVAL.

*/
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

/* Implements an emulation of the mknod system call for regular files
   on the filesystem of size fssize pointed to by fsptr.

   This function is called only for the creation of regular files.

   If a file gets created, it is of size zero and has default
   ownership and mode bits.

   The call creates the file indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 mknod.

*/
int __myfs_mknod_implem(void *fsptr, size_t fssize, int *errnoptr, const char *path) {
    struct myfs_super *super = initialize_myfs(fsptr, fssize);

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
        update_time(existing_node, 1);
        *errnoptr = EEXIST;
        free(last_token);
        return -1;
    }

    if (strlen(last_token) > NAME_MAX_LEN) {
        *errnoptr = ENAMETOOLONG;
        free(last_token);
        return -1;
    }

    // Validate path
    if (strcmp(path, "/") == 0) {
        *errnoptr = EINVAL; // Cannot create a file named "/"
        return -1;
    }

    // Check if file already exists
    if (find_node(fsptr, path) != NULL) {
        *errnoptr = EEXIST;
        return -1;
    }

    // Find parent directory
    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        *errnoptr = ENOMEM;
        return -1;
    }

    // Get name 
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

    // Check available space
    size_t required_space = sizeof(struct myfs_node) +
                            (parent_dir->data.directory.number_children + 1) * sizeof(myfs_off_t);

    if (super->free_memory + required_space > super->size) {
        *errnoptr = ENOSPC;
        return -1;
    }

    // Allocate and initialize new file node
    struct myfs_node *new_node = off_to_ptr(fsptr, super->free_memory);
    super->free_memory += sizeof(struct myfs_node);

    memset(new_node, 0, sizeof(struct myfs_node));
    strncpy(new_node->name, actual_name, NAME_MAX_LEN);
    new_node->is_file = 1;
    clock_gettime(CLOCK_REALTIME, &new_node->times[0]);
    new_node->times[1] = new_node->times[0];

    // Update parent directory children list
    size_t child_array_size = (parent_dir->data.directory.number_children + 1) * sizeof(myfs_off_t);

    // Allocate new block for children
    myfs_off_t new_children_offset = super->free_memory;
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
    super->free_memory += child_array_size;

    return 0;
}




/* Implements an emulation of the unlink system call for regular files
   on the filesystem of size fssize pointed to by fsptr.

   This function is called only for the deletion of regular files.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 unlink.

*/
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
        // Mark the block as free
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
int __myfs_rmdir_implem(void *fsptr, size_t fssize, int *errnoptr, const char *path) {
    
    // Find the parent directory and the directory to be removed
    char *dir_name;
    struct myfs_node *parent_node = find_parent_node(fsptr, path, &dir_name);
    
    if (parent_node == NULL) {
        *errnoptr = ENOENT;
        free(dir_name);
        return -1;
    }
    
    if (parent_node->is_file) {
        *errnoptr = ENOTDIR;
        free(dir_name);
        return -1;
    }
    
    // Find the directory node to be removed
    struct myfs_node *dir_node = get_node(fsptr, &parent_node->data.directory, dir_name);
    
    if (dir_node == NULL) {
        *errnoptr = ENOENT;
        free(dir_name);
        return -1;
    }
    
    if (dir_node->is_file) {
        *errnoptr = ENOTDIR;
        free(dir_name);
        return -1;
    }
    
    // Check if the directory is empty
    if (dir_node->data.directory.number_children > 0) {
        *errnoptr = ENOTEMPTY;
        free(dir_name);
        return -1;
    }
    
    // Remove the directory from the parent's children list
    myfs_off_t *parent_children = off_to_ptr(fsptr, parent_node->data.directory.children);
    size_t num_children = parent_node->data.directory.number_children;
    
    for (size_t i = 0; i < num_children; i++) {
        if (parent_children[i] == ptr_to_off(fsptr, dir_node)) {
            // Move the last child to this position and decrease the count
            parent_children[i] = parent_children[num_children - 1];
            parent_node->data.directory.number_children--;
            break;
        }
    }
    
    // Free the directory node
    memset(dir_node, 0, sizeof(struct myfs_node));
    
    // Update parent directory's modification time
    update_time(parent_node, 1);
    
    free(dir_name);
    return 0;
}


/* Implements an emulation of the mkdir system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call creates the directory indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 mkdir.

*/
int __myfs_mkdir_implem(void *fsptr, size_t fssize, int *errnoptr, const char *path) {
    struct myfs_super *super = initialize_myfs(fsptr, fssize);

    // Find the parent directory and the last token (directory name)
    char *last_token;
    struct myfs_node *parent_node = find_parent_node(fsptr, path, &last_token);
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

    // Check if the directory name already exists
    struct myfs_node *existing_node = get_node(fsptr, &parent_node->data.directory, last_token);
    if (existing_node != NULL) {
        *errnoptr = EEXIST;
        free(last_token);
        return -1;
    }

    // Check if the directory name is too long
    if (strlen(last_token) > NAME_MAX_LEN) {
        *errnoptr = ENAMETOOLONG;
        free(last_token);
        return -1;
    }

    // Check if the path is the root directory
    if (strcmp(path, "/") == 0) {
        *errnoptr = EEXIST; // Cannot create a directory named "/"
        free(last_token);
        return -1;
    }

    // Calculate the required space for the new directory node and its children list
    size_t required_space = sizeof(struct myfs_node) + sizeof(myfs_off_t);

    if (super->free_memory + required_space > super->size) {
        *errnoptr = ENOSPC;
        free(last_token);
        return -1;
    }

    // Allocate and initialize the new directory node
    struct myfs_node *new_dir = off_to_ptr(fsptr, super->free_memory);
    super->free_memory += sizeof(struct myfs_node);

    memset(new_dir, 0, sizeof(struct myfs_node));
    strncpy(new_dir->name, last_token, NAME_MAX_LEN);
    new_dir->is_file = 0;
    update_time(new_dir, 1);

    // Initialize the new directory's children list
    myfs_off_t *children = off_to_ptr(fsptr, super->free_memory);
    *children = 0;
    new_dir->data.directory.children = ptr_to_off(fsptr, children);
    new_dir->data.directory.number_children = 0;
    super->free_memory += sizeof(myfs_off_t);

    // Update the parent directory's children list
    size_t child_array_size = (parent_node->data.directory.number_children + 1) * sizeof(myfs_off_t);

    // Allocate new block for children
    myfs_off_t new_children_offset = super->free_memory;
    myfs_off_t *new_children = off_to_ptr(fsptr, new_children_offset);

    if (parent_node->data.directory.number_children > 0) {
        // Copy existing children data to the new block
        myfs_off_t *old_children = off_to_ptr(fsptr, parent_node->data.directory.children);
        memcpy(new_children, old_children, parent_node->data.directory.number_children * sizeof(myfs_off_t));
    }

    // Update the children list with the new child
    new_children[parent_node->data.directory.number_children] = ptr_to_off(fsptr, new_dir);
    parent_node->data.directory.children = new_children_offset;
    parent_node->data.directory.number_children++;
    super->free_memory += child_array_size;

    // Update the parent directory's modification time
    update_time(parent_node, 1);

    free(last_token);
    return 0;
}



/* Implements an emulation of the rename system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call moves the file or directory indicated by from to to.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   Caution: the function does more than what is hinted to by its name.
   In cases the from and to paths differ, the file is moved out of 
   the from path and added to the to path.

   The error codes are documented in man 2 rename.

*/
int __myfs_rename_implem(void *fsptr, size_t fssize, int *errnoptr,
                         const char *from, const char *to) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the truncate system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call changes the size of the file indicated by path to offset
   bytes.

   When the file becomes smaller due to the call, the extending bytes are
   removed. When it becomes larger, zeros are appended.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 truncate.

*/
int __myfs_truncate_implem(void *fsptr, size_t fssize, int *errnoptr,
                           const char *path, off_t offset) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the open system call on the filesystem 
   of size fssize pointed to by fsptr, without actually performing the opening
   of the file (no file descriptor is returned).

   The call just checks if the file (or directory) indicated by path
   can be accessed, i.e. if the path can be followed to an existing
   object for which the access rights are granted.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The two only interesting error codes are 

   * EFAULT: the filesystem is in a bad state, we can't do anything

   * ENOENT: the file that we are supposed to open doesn't exist (or a
             subpath).

   It is possible to restrict ourselves to only these two error
   conditions. It is also possible to implement more detailed error
   condition answers.

   The error codes are documented in man 2 open.

*/
int __myfs_open_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the read system call on the filesystem 
   of size fssize pointed to by fsptr.

   The call copies up to size bytes from the file indicated by 
   path into the buffer, starting to read at offset. See the man page
   for read for the details when offset is beyond the end of the file etc.
   
   On success, the appropriate number of bytes read into the buffer is
   returned. The value zero is returned on an end-of-file condition.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 read.

*/
int __myfs_read_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path, char *buf, size_t size, off_t offset) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the write system call on the filesystem 
   of size fssize pointed to by fsptr.

   The call copies up to size bytes to the file indicated by 
   path into the buffer, starting to write at offset. See the man page
   for write for the details when offset is beyond the end of the file etc.
   
   On success, the appropriate number of bytes written into the file is
   returned. The value zero is returned on an end-of-file condition.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 write.

*/
int __myfs_write_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path, const char *buf, size_t size, off_t offset) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the utimensat system call on the filesystem 
   of size fssize pointed to by fsptr.

   The call changes the access and modification times of the file
   or directory indicated by path to the values in ts.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 utimensat.

*/
int __myfs_utimens_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, const struct timespec ts[2]) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the statfs system call on the filesystem 
   of size fssize pointed to by fsptr.

   The call gets information of the filesystem usage and puts in 
   into stbuf.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 statfs.

   Essentially, only the following fields of struct statvfs need to be
   supported:

   f_bsize   fill with what you call a block (typically 1024 bytes)
   f_blocks  fill with the total number of blocks in the filesystem
   f_bfree   fill with the free number of blocks in the filesystem
   f_bavail  fill with same value as f_bfree
   f_namemax fill with your maximum file/directory name, if your
             filesystem has such a maximum

*/
int __myfs_statfs_implem(void *fsptr, size_t fssize, int *errnoptr,
                         struct statvfs* stbuf) {
  /* STUB */
  return -1;
}

