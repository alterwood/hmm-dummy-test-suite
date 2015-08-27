/*
 * Copyright 2013 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors: Jérôme Glisse <jglisse@redhat.com>
 */
#define _GNU_SOURCE

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <setjmp.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/hmm_dummy.h>

#include "hmm_test_framework.h"

#define MAX_RETRY 16

#define ALIGN(x, a) (((x) + (a - 1)) & (~((a) - 1)))

static int _hmm_exit_ok = 0;
static jmp_buf _hmm_exit_env;
static unsigned page_size = 0;
static unsigned page_shift;
static unsigned long page_mask;


static inline void hmm_init_page_info(void)
{
    if (page_size) {
        return;
    }
    page_size = sysconf(_SC_PAGE_SIZE);
    page_shift = ffs(page_size) - 1;
    page_mask = ~((unsigned long)(page_size - 1));
}

static void hmm_exit(void)
{
    if (_hmm_exit_ok) {
        longjmp(_hmm_exit_env, -1);
    }
    exit(-1);
}

static int hmm_dummy_ctx_register(struct hmm_ctx *ctx)
{
    int ret;

    hmm_init_page_info();

    ret = ioctl(ctx->fd, HMM_DUMMY_EXPOSE_MM, NULL);
    if (ret) {
        fprintf(stderr, "hmm dummy driver register failed (%d)\n", ret);
    } else {
        ctx->pid = getpid();
    }
    return ret;
}

int hmm_ctx_init(struct hmm_ctx *ctx)
{
    char pathname[32];

    hmm_init_page_info();

    if (setjmp(_hmm_exit_env)) {
        return -1;
    }
    _hmm_exit_ok = 1;

    snprintf(pathname, sizeof(pathname), "/dev/hmm_dummy_device%d%d", 0, 0);
    ctx->fd = open(pathname, O_RDWR, 0);
    if (ctx->fd < 0) {
        fprintf(stderr, "could not open hmm dummy driver (%s)\n", pathname);
        return -1;
    }

    return hmm_dummy_ctx_register(ctx);
}

void hmm_ctx_fini(struct hmm_ctx *ctx)
{
    close(ctx->fd);
    ctx->fd = -1;
}

unsigned long hmm_buffer_nbytes(struct hmm_buffer *buffer)
{
    return buffer->npages << page_shift;
}

struct hmm_buffer *hmm_buffer_new_anon(const char *name, unsigned long nbytes)
{
    struct hmm_buffer *buffer;
    unsigned long npages;

    hmm_init_page_info();

    if (!nbytes) {
        fprintf(stderr, "(EE) %s(%s).nbytes -> %ld\n", __func__, name, nbytes);
        hmm_exit();
    }

    npages = ALIGN(nbytes, page_size) >> page_shift;

    buffer = malloc(sizeof(*buffer));
    if (buffer == NULL) {
        fprintf(stderr, "(EE) %s(%s).malloc(struct)\n", __func__, name);
        hmm_exit();
    }

    buffer->fd = -1;
    buffer->name = name;
    buffer->npages = npages;
    buffer->mirror = malloc(npages << page_shift);
    if (buffer->mirror == NULL) {
        fprintf(stderr, "(EE) %s(%s).malloc(mirror)\n", __func__, name);
        free(buffer);
        hmm_exit();
    }

    buffer->ptr = mmap(0, npages << page_shift,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    if (buffer->ptr == MAP_FAILED) {
        fprintf(stderr, "(EE) %s(%s).mmap(%ld)\n", __func__, name, npages);
        free(buffer);
        hmm_exit();
    }

    return buffer;
}

struct hmm_buffer *hmm_buffer_new_share(const char *name, unsigned long nbytes)
{
    struct hmm_buffer *buffer;
    unsigned long npages;

    hmm_init_page_info();

    if (!nbytes) {
        fprintf(stderr, "(EE) %s(%s).nbytes -> %ld\n", __func__, name, nbytes);
        hmm_exit();
    }

    npages = ALIGN(nbytes, page_size) >> page_shift;

    buffer = malloc(sizeof(*buffer));
    if (buffer == NULL) {
        fprintf(stderr, "(EE) %s(%s).malloc(struct)\n", __func__, name);
        hmm_exit();
    }

    buffer->fd = -1;
    buffer->name = name;
    buffer->npages = npages;
    buffer->mirror = malloc(npages << page_shift);
    if (buffer->mirror == NULL) {
        fprintf(stderr, "(EE) %s(%s).malloc(mirror)\n", __func__, name);
        free(buffer);
        hmm_exit();
    }

    buffer->ptr = mmap(0, npages << page_shift,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS,
                       -1, 0);
    if (buffer->ptr == MAP_FAILED) {
        fprintf(stderr, "(EE) %s(%s).mmap(%ld)\n", __func__, name, npages);
        free(buffer);
        hmm_exit();
    }

    return buffer;
}

struct hmm_buffer *hmm_buffer_new_file(const char *name, int fd, unsigned long nbytes)
{
    struct hmm_buffer *buffer;
    unsigned long npages;

    hmm_init_page_info();

    if (!nbytes) {
        fprintf(stderr, "(EE) %s(%s).nbytes -> %ld\n", __func__, name, nbytes);
        hmm_exit();
    }

    npages = ALIGN(nbytes, page_size) >> page_shift;

    buffer = malloc(sizeof(*buffer));
    if (buffer == NULL) {
        fprintf(stderr, "(EE) %s(%s).malloc(struct)\n", __func__, name);
        hmm_exit();
    }

    buffer->fd = fd;
    buffer->name = name;
    buffer->npages = npages;
    buffer->mirror = malloc(npages << page_shift);
    if (buffer->mirror == NULL) {
        fprintf(stderr, "(EE) %s(%s).malloc(mirror)\n", __func__, name);
        free(buffer);
        hmm_exit();
    }

    buffer->ptr = mmap(0, npages << page_shift,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (buffer->ptr == MAP_FAILED) {
        fprintf(stderr, "(EE) %s(%s).mmap(%ld)\n", __func__, name, npages);
        free(buffer);
        hmm_exit();
    }

    return buffer;
}

int hmm_buffer_mirror_read(struct hmm_ctx *ctx, struct hmm_buffer *buffer)
{
    struct hmm_dummy_read read;
    int ret;

    read.address = (uintptr_t)buffer->ptr;
    read.size = hmm_buffer_nbytes(buffer);
    read.ptr = (uintptr_t)buffer->mirror;

    do {
        ret = ioctl(ctx->fd, HMM_DUMMY_READ, &read);
        if (ret && errno != EINTR) {
            return ret;
        }
    } while (errno == EINTR);

    buffer->nsys_pages = read.nsys_pages;
    buffer->nfaulted_sys_pages = read.nfaulted_sys_pages;
    buffer->ndev_pages = read.ndev_pages;
    buffer->nfaulted_dev_pages = read.nfaulted_dev_pages;

    return 0;
}

int hmm_buffer_mprotect(struct hmm_buffer *buffer, int prot)
{
    if (mprotect(buffer->ptr, hmm_buffer_nbytes(buffer), prot)) {
        return -errno;
    }
    return 0;
}

int hmm_buffer_mirror_write(struct hmm_ctx *ctx, struct hmm_buffer *buffer)
{
    struct hmm_dummy_write write;
    int ret;

    write.address = (uintptr_t)buffer->ptr;
    write.size = hmm_buffer_nbytes(buffer);
    write.ptr = (uintptr_t)buffer->mirror;

    do {
        ret = ioctl(ctx->fd, HMM_DUMMY_WRITE, &write);
        if (ret && errno != EINTR) {
            return ret;
        }
    } while (errno == EINTR);

    buffer->nsys_pages = write.nsys_pages;
    buffer->nfaulted_sys_pages = write.nfaulted_sys_pages;
    buffer->ndev_pages = write.ndev_pages;
    buffer->nfaulted_dev_pages = write.nfaulted_dev_pages;

    return 0;
}

int hmm_buffer_mirror_migrate_to(struct hmm_ctx *ctx, struct hmm_buffer *buffer)
{
    struct hmm_dummy_migrate migrate;
    int ret;

    migrate.address = (uintptr_t)buffer->ptr;
    migrate.size = hmm_buffer_nbytes(buffer);

    ret = ioctl(ctx->fd, HMM_DUMMY_MIGRATE_TO, &migrate);
    if (ret) {
        fprintf(stderr, "(EE:%4d) %d\n", __LINE__, ret);
        hmm_exit();
    }

    buffer->nfaulted_dev_pages = migrate.nfaulted_dev_pages;

    return 0;
}

void hmm_buffer_free(struct hmm_buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }

    munmap(buffer->ptr, buffer->npages << page_shift);
    free(buffer->mirror);
    free(buffer);
}

int hmm_create_file(char *path, unsigned long size)
{
    unsigned i;
    int fd;

    hmm_init_page_info();

    size = ALIGN(size, page_size);

    for (i = 0; i < 999; ++i) {
        sprintf(path, "/tmp/hmmtmp%d", i);
        fd = open(path, O_CREAT | O_EXCL | O_RDWR, S_IWUSR | S_IRUSR);
        if (fd >= 0) {
            int r;

            do {
                r = truncate(path, size);
            } while (r == -1 && errno == EINTR);
            if (!r) {
                return fd;
            }
            fprintf(stderr, "(EE:%4d) truncate failed for %ld bytes (%d)\n",
                    __LINE__, size, errno);
            close(fd);
            unlink(path);
            return -1;
        }
    }
    return -1;
}

unsigned hmm_random(void)
{
    static int fd = -1;
    unsigned r;

    if (fd < 0) {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "%s:%d failed to open /dev/urandom\n",
                    __FILE__, __LINE__);
            exit(-1);
        }
    }
    read(fd, &r, sizeof(r));
    return r;
}

void hmm_nanosleep(unsigned n)
{
    struct timespec t;

    t.tv_sec = 0;
    t.tv_nsec = n;
    nanosleep(&t , NULL);
}
