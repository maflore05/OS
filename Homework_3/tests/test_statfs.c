#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/statvfs.h>

typedef struct myfs_superblock {
    size_t total_blocks;
    size_t free_blocks;
    size_t block_size;
    size_t namemax;
} myfs_superblock_t;

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

int main() {
    myfs_superblock_t fs = {
        .total_blocks = 10000,
        .free_blocks = 8000,
        .block_size = 1024,
        .namemax = 255
    };

    struct statvfs stbuf;
    int err;

    printf("Testing statfs on mock filesystem:\n");
    int res = __myfs_statfs_implem(&fs, sizeof(fs), &err, &stbuf);

    if (res == 0) {
        printf("Filesystem stats retrieved successfully:\n");
        printf("Block size: %lu\n", stbuf.f_bsize);
        printf("Total blocks: %lu\n", stbuf.f_blocks);
        printf("Free blocks: %lu\n", stbuf.f_bfree);
        printf("Available blocks: %lu\n", stbuf.f_bavail);
        printf("Maximum filename length: %lu\n", stbuf.f_namemax);
    } else {
        printf("Failed to retrieve filesystem stats, error: %d\n", err);
    }

    printf("\nTest 2: Statfs with NULL filesystem pointer\n");
    res = __myfs_statfs_implem(NULL, sizeof(fs), &err, &stbuf);
    if (res == -1 && err == EFAULT) {
        printf("Success: Invalid filesystem pointer (EFAULT)\n");
    } else {
        printf("Unexpected result: %d, error: %d\n", res, err);
    }

    printf("\nTest 3: Statfs with NULL statvfs pointer\n");
    res = __myfs_statfs_implem(&fs, sizeof(fs), &err, NULL);
    if (res == -1 && err == EFAULT) {
        printf("Success: Invalid statvfs pointer (EFAULT)\n");
    } else {
        printf("Unexpected result: %d, error: %d\n", res, err);
    }

    printf("\nTest 4: Statfs with empty filesystem (all blocks free)\n");
    myfs_superblock_t empty_fs = {
        .total_blocks = 10000,
        .free_blocks = 10000,
        .block_size = 1024,
        .namemax = 255
    };

    res = __myfs_statfs_implem(&empty_fs, sizeof(empty_fs), &err, &stbuf);
    if (res == 0) {
        printf("Success: Stats retrieved for empty filesystem\n");
        printf("Free blocks: %lu\n", stbuf.f_bfree);
    } else {
        printf("Unexpected result: %d, error: %d\n", res, err);
    }

    return 0;
}

