#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_rwlock_t inode_locks[INODE_TABLE_SIZE];
static pthread_rwlock_t open_file_table_lock;

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    pthread_rwlock_init(&open_file_table_lock, NULL);
    for (size_t i = 0; i < INODE_TABLE_SIZE; i += 1) {
        pthread_rwlock_init(&inode_locks[i], NULL);
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    pthread_rwlock_destroy(&open_file_table_lock);
    for (size_t i = 0; i < INODE_TABLE_SIZE; i += 1) {
        pthread_rwlock_destroy(&inode_locks[i]);
    }
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}


int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    pthread_rwlock_rdlock(&inode_locks[0]);
    int ret = find_in_dir(ROOT_DIR_INUM, name);
    pthread_rwlock_unlock(&inode_locks[0]);
    return ret;
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            return -1;
        }
        if (flags & TFS_O_TRUNC) {
            pthread_rwlock_wrlock(&inode_locks[inum]);
        }
        else {
            pthread_rwlock_rdlock(&inode_locks[inum]);
        }

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                if (inode_free_blocks(inode) == -1) {
                    return -1;
                }
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
        pthread_rwlock_unlock(&inode_locks[inum]);
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        pthread_rwlock_wrlock(&inode_locks[0]);
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        pthread_rwlock_unlock(&inode_locks[0]);
        offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    pthread_rwlock_wrlock(&open_file_table_lock);
    int fd = add_to_open_file_table(inum, offset);
    pthread_rwlock_unlock(&open_file_table_lock);
    return fd;

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}

static void *inode_data_block_get(inode_t *inode, size_t i)
{
    if (i < 10) {
        return data_block_get(inode->i_data_blocks[i]);
    }
    int *indirect_blocks = (int *)data_block_get(inode->i_data_blocks[10]);
    if (indirect_blocks == NULL) {
        return NULL;
    }
    return data_block_get(indirect_blocks[i - 10]);
}

int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

static void read_or_write_aux(void *block, void *buffer, size_t size, int write)
{
    if (write) {
        memcpy(block, buffer, size);
        return;
    }
    memcpy(buffer, block, size);
}

static int read_or_write(inode_t *inode, size_t offset, void *buffer, size_t size, int write)
{
    size_t new_offset = offset + size;
    if (write) {
        size_t new_blocks = (new_offset + BLOCK_SIZE - 1) / BLOCK_SIZE;
        for (size_t i = (inode->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE; i < new_blocks; i += 1) {
            /* Allocate new blocks if necessary */
            inode->i_data_blocks[i] = data_block_alloc();
        }
    }

    size_t first_block = offset / BLOCK_SIZE;
    if ((offset & -(size_t)BLOCK_SIZE) == (new_offset & -(size_t)BLOCK_SIZE)) {
        void *block = inode_data_block_get(inode, first_block);
        if (block == NULL) {
            return -1;
        }
        read_or_write_aux(block + offset, buffer, size, write);
    }
    else {
        void *block = inode_data_block_get(inode, first_block);
        if (block == NULL) {
            return -1;
        }
        read_or_write_aux(block + offset, buffer,
            BLOCK_SIZE - (offset & (BLOCK_SIZE - 1)), write);
        for (size_t i = offset / BLOCK_SIZE + 1; i <= new_offset / BLOCK_SIZE - 1; i += 1) {
            block = inode_data_block_get(inode, i);
            if (block == NULL) {
                return -1;
            }
            read_or_write_aux(block, buffer + i * BLOCK_SIZE - offset, BLOCK_SIZE, write);
        }
        size_t final_bytes = new_offset & (BLOCK_SIZE - 1);
        if (final_bytes) {
            read_or_write_aux(block, buffer + size - final_bytes, final_bytes, write);
        }
    }
    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    pthread_mutex_lock(&file->of_lock);
    /* Determine how many bytes to write */
    if (to_write + file->of_offset > MAX_FILE_SIZE) {
        to_write = MAX_FILE_SIZE - file->of_offset;
    }

    if (to_write > 0) {
        pthread_rwlock_wrlock(&inode_locks[file->of_inumber]);
        read_or_write(inode, file->of_offset, (void *)buffer, to_write, 1);
        pthread_rwlock_unlock(&inode_locks[file->of_inumber]);
    }

    /* The offset associated with the file handle is
     * incremented accordingly */
    file->of_offset += to_write;
    if (file->of_offset > inode->i_size) {
        inode->i_size = file->of_offset;
    }

    pthread_mutex_unlock(&file->of_lock);
    return (ssize_t)to_write;
}


ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to read */
    pthread_mutex_lock(&file->of_lock);
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }
    if (file->of_offset + to_read > MAX_FILE_SIZE) {
        to_read = MAX_FILE_SIZE - file->of_offset;
    }

    if (to_read > 0) {
        /* Perform the actual read */
        read_or_write(inode, file->of_offset, buffer, to_read, 0);
        /* The offset associated with the file handle is
         * incremented accordingly */
        file->of_offset += to_read;
    }

    pthread_mutex_unlock(&file->of_lock);
    return (ssize_t)to_read;
}

int tfs_copy_to_external_fs(char const *source_path, char const *dest_path)
{
    int inum;

    /* Checks if the path name is valid */
    if (!valid_pathname(source_path)) {
        return -1;
    }

    inum = tfs_lookup(source_path);
    if (inum == -1) {
        return -1;
    }

    FILE *file = fopen(dest_path, "w");
    if (file == NULL) {
        return -1;
    }
    inode_t *inode = inode_get(inum);
    pthread_rwlock_rdlock(&inode_locks[inum]);
    size_t blocks = (inode->i_size + BLOCK_SIZE - 1) / (size_t)BLOCK_SIZE;
    size_t direct_blocks = blocks < 10 ? blocks : 10;
    for (size_t i = 0; i < direct_blocks; i += 1) {
        if (fwrite(data_block_get(inode->i_data_blocks[i]), BLOCK_SIZE, 1, file) != 1) {
            return -1;
        }
    }
    if (blocks > 10) {
        int* indirect_blocks = (int*)data_block_get(inode->i_data_blocks[10]);
        for (size_t i = 0; i < blocks - 10; i += 1) {
            if (fwrite(data_block_get(indirect_blocks[i]), BLOCK_SIZE, 1, file) != 1) {
                return -1;
            }
        }
    }
    pthread_rwlock_unlock(&inode_locks[inum]);
    fclose(file);
    return 0;
}
