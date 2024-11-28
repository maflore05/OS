#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/statvfs.h>

struct myfs_metadata {
    size_t total_blocks;
    size_t free_blocks;
};

int __myfs_statfs_implem(void *fsptr, size_t fssize, int *errnoptr,
                         struct statvfs* stbuf) {
    if (!fsptr || !stbuf || !errnoptr) {
        if (errnoptr) *errnoptr = EFAULT;
        return -1;
    }

    if (fssize < sizeof(struct myfs_metadata)) {
        *errnoptr = EIO;
        return -1;
    }

    struct myfs_metadata *metadata = (struct myfs_metadata *)fsptr;

    memset(stbuf, 0, sizeof(struct statvfs));
    stbuf->f_bsize = 1024;
    stbuf->f_blocks = metadata->total_blocks;
    stbuf->f_bfree = metadata->free_blocks;
    stbuf->f_bavail = metadata->free_blocks;
    stbuf->f_namemax = 255;

    return 0;
}

int main() {
    struct myfs_metadata fs_metadata = {
        .total_blocks = 1000,
        .free_blocks = 500,
    };

    struct statvfs stbuf;
    int err;

    printf("Test 1: Statfs on valid filesystem\n");
    int result = __myfs_statfs_implem(&fs_metadata, sizeof(fs_metadata), &err, &stbuf);
    if (result == 0) {
        printf("Success: Filesystem stats retrieved\n");
        printf("f_bsize: %ld, f_blocks: %ld, f_bfree: %ld, f_bavail: %ld, f_namemax: %ld\n",
               stbuf.f_bsize, stbuf.f_blocks, stbuf.f_bfree, stbuf.f_bavail, stbuf.f_namemax);
    } else {
        printf("Error: %d\n", err);
    }

    printf("\nTest 2: Statfs with insufficient memory\n");
    result = __myfs_statfs_implem(&fs_metadata, sizeof(fs_metadata) - 1, &err, &stbuf);
    if (result == -1 && err == EIO) {
        printf("Success: Insufficient memory (EIO)\n");
    } else {
        printf("Unexpected result: %d, error: %d\n", result, err);
    }

    printf("\nTest 3: Statfs with invalid filesystem pointer (NULL)\n");
    result = __myfs_statfs_implem(NULL, sizeof(fs_metadata), &err, &stbuf);
    if (result == -1 && err == EFAULT) {
        printf("Success: Invalid filesystem pointer (EFAULT)\n");
    } else {
        printf("Unexpected result: %d, error: %d\n", result, err);
    }

    printf("\nTest 4: Statfs with invalid statvfs pointer (NULL)\n");
    result = __myfs_statfs_implem(&fs_metadata, sizeof(fs_metadata), &err, NULL);
    if (result == -1 && err == EFAULT) {
        printf("Success: Invalid statvfs pointer (EFAULT)\n");
    } else {
        printf("Unexpected result: %d, error: %d\n", result, err);
    }

    return 0;
}

