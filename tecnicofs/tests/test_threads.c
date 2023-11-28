#include "operations.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void *thread(void *arg) {
    int n = (int)(intptr_t)arg;
    char name[4], buffer[6];
    snprintf(name, 4, "/f%d", n);
    int fd = tfs_open(name, TFS_O_CREAT);
    if (fd == -1) {
        fprintf(stderr, "Cannot create %s\n", name);
        exit(2);
    }
    tfs_write(fd, "grupo3", 6);
    tfs_close(fd);
    if ((fd = tfs_open(name, 0)) == -1) {
        fprintf(stderr, "Cannot open %s for reading\n", name);
        exit(2);
    }
    tfs_read(fd, buffer, 6);
    if (memcmp(buffer, "grupo3", 6)) {
        fputs("Read returned incorrect result\n", stderr);
        exit(2);
    }
    tfs_close(fd);
    return NULL;
}

void *thread2(void *arg) {
    int n = (int)(intptr_t)arg;
    char name[4];
    snprintf(name, 4, "/a%d", n >> 1);
    int fd = tfs_open(name, n & 1 ? TFS_O_APPEND : 0);
    if (fd == -1) {
        fprintf(stderr, "Cannot open %s in %s mode\n", name,
                n & 1 ? "append" : "default");
        exit(2);
    }
    if (tfs_write(fd, "grupo3", 6) != 6) {
        fputs("Write returned incorrect result\n", stderr);
        exit(2);
    }
    if (tfs_close(fd) != 0) {
        fprintf(stderr, "Cannot close %s\n", name);
        exit(2);
    }
    return NULL;
}

void *thread3(void *arg) {
    int n = (int)(intptr_t)arg;
    char name[4];
    snprintf(name, 4, "/b%d", n);
    int fd = tfs_open(name, TFS_O_CREAT);
    if (fd == -1) {
        fprintf(stderr, "Cannot create %s\n", name);
        exit(2);
    }
    if (tfs_write(fd, "test", 4) != 4) {
        fputs("Write returned incorrect result\n", stderr);
        exit(2);
    }
    if (tfs_close(fd) != 0) {
        fprintf(stderr, "Cannot close %s\n", name);
        exit(2);
    }
    if (tfs_copy_to_external_fs(name, name + 1) != 0) {
        fprintf(stderr, "Cannot copy %s to external file %s\n", name, name + 1);
        exit(2);
    }
    snprintf(name, 4, "c%d", n);
    if (tfs_copy_to_external_fs("/c", name) != 0) {
        fprintf(stderr, "Cannot copy /c to external file %s\n", name);
        exit(2);
    }
    return NULL;
}

int main() {
    pthread_t threads[6];
    if (tfs_init() != 0) {
        fputs("Cannot initialize TecnicoFS\n", stderr);
        return 2;
    }
    for (int i = 0; i < 3; i += 1) {
        pthread_create(&threads[i], NULL, thread, (void *)(intptr_t)i);
    }
    for (int i = 0; i < 3; i += 1) {
        pthread_join(threads[i], NULL);
    }
    for (char i = '0'; i < '3'; i += (char)1) {
        char name[4];
        snprintf(name, 4, "/a%c", i);
        int fd = tfs_open(name, TFS_O_CREAT);
        if (fd == -1) {
            fprintf(stderr, "Cannot create %s\n", name);
            return 2;
        }
        if (tfs_write(fd, "aaaaaa", 6) != 6) {
            fputs("Write returned incorrect result\n", stderr);
            return 2;
        }
        if (tfs_close(fd) != 0) {
            fprintf(stderr, "Cannot close %s\n", name);
            return 2;
        }
    }
    for (int i = 0; i < 6; i += 1) {
        pthread_create(&threads[i], NULL, thread2, (void *)(intptr_t)i);
    }
    for (int i = 0; i < 6; i += 1) {
        pthread_join(threads[i], NULL);
    }
    for (char i = '0'; i < '3'; i += (char)1) {
        char name[4];
        char buffer[12];
        snprintf(name, 4, "/a%c", i);
        int fd = tfs_open(name, 0);
        if (fd == -1) {
            fprintf(stderr, "Cannot open %s\n", name);
            return 2;
        }
        if (tfs_read(fd, buffer, 12) != 12) {
            fputs("Read returned incorrect result\n", stderr);
            return 2;
        }
        if (memcmp(buffer, "grupo3grupo3", 12) != 0) {
            fputs("Read wrote incorrect result\n", stderr);
            return 2;
        }
        if (tfs_close(fd) != 0) {
            fprintf(stderr, "Cannot close %s\n", name);
            return 2;
        }
    }
    int fd = tfs_open("/c", TFS_O_CREAT);
    if (fd == -1) {
        fputs("Cannot create /c\n", stderr);
        return 2;
    }
    if (tfs_write(fd, "test", 4) != 4) {
        fputs("Write returned incorrect result\n", stderr);
        return 2;
    }
    if (tfs_close(fd) != 0) {
        fputs("Cannot close /c\n", stderr);
        return 2;
    }
    for (int i = 0; i < 3; i += 1) {
        pthread_create(&threads[i], NULL, thread3, (void *)(intptr_t)i);
    }
    for (int i = 0; i < 3; i += 1) {
        pthread_join(threads[i], NULL);
    }
    if (tfs_destroy() != 0) {
        fputs("Cannot destroy TFS\n", stderr);
        return 2;
    }
    puts("Successful test.");
    return 0;
}
