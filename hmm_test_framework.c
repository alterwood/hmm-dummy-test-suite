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

static jmp_buf _hmm_exit_env;

static void hmm_exit(void)
{
    longjmp(_hmm_exit_env, -1);
}

static int hmm_dummy_ctx_register(struct hmm_ctx *ctx)
{
    int ret;

    ret = ioctl(ctx->fd, HMM_DUMMY_EXPOSE_MM, NULL);
    if (ret) {
        fprintf(stderr, "hmm dummy driver register failed (%d)\n", ret);
    } else {
        ctx->pid = getpid();
    }
    return ret;
}

static int hmm_ctx_init(struct hmm_ctx *ctx)
{
    char pathname[32];

    snprintf(pathname, sizeof(pathname), "/dev/hmm_dummy_device%d%d", 0, 0);
    ctx->fd = open(pathname, O_RDWR, 0);
    if (ctx->fd < 0) {
        fprintf(stderr, "could not open hmm dummy driver (%s)\n", pathname);
        return -1;
    }
    ctx->page_size = sysconf(_SC_PAGE_SIZE);
    ctx->page_shift = ffs(ctx->page_size) - 1;
    ctx->page_mask = ~((unsigned long)(ctx->page_size - 1));

    return hmm_dummy_ctx_register(ctx);
}

void hmm_ctx_fini(struct hmm_ctx *ctx)
{
    close(ctx->fd);
    ctx->fd = -1;
}

struct hmm_buffer *hmm_buffer_new_anon(struct hmm_ctx *ctx,
                                       const char *name,
                                       unsigned long npages)
{
    struct hmm_buffer *buffer;

    if (!npages) {
        fprintf(stderr, "(EE) %s(%s).npages -> %ld\n", __func__, name, npages);
        hmm_exit();
    }

    buffer = malloc(sizeof(*buffer));
    if (buffer == NULL) {
        fprintf(stderr, "(EE) %s(%s).malloc(struct)\n", __func__, name);
        hmm_exit();
    }

    buffer->fd = -1;
    buffer->name = name;
    buffer->npages = npages;
    buffer->mirror = malloc(npages << ctx->page_shift);
    if (buffer->mirror == NULL) {
        fprintf(stderr, "(EE) %s(%s).malloc(mirror)\n", __func__, name);
        free(buffer);
        hmm_exit();
    }

    buffer->ptr = mmap(0, npages << ctx->page_shift,
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

int hmm_buffer_mirror_read(struct hmm_ctx *ctx, struct hmm_buffer *buffer)
{
    struct hmm_dummy_read read;
    int ret;

    read.address = (uintptr_t)buffer->ptr;
    read.size = hmm_buffer_nbytes(ctx, buffer);
    read.ptr = (uintptr_t)buffer->mirror;

    ret = ioctl(ctx->fd, HMM_DUMMY_READ, &read);
    if (ret) {
        fprintf(stderr, "(EE:%4d) %d\n", __LINE__, ret);
        hmm_exit();
    }

    buffer->nsys_pages = read.nsys_pages;
    buffer->nfaulted_sys_pages = read.nfaulted_sys_pages;

    return 0;
}

int hmm_buffer_mirror_write(struct hmm_ctx *ctx, struct hmm_buffer *buffer)
{
    struct hmm_dummy_write write;
    int ret;

    write.address = (uintptr_t)buffer->ptr;
    write.size = hmm_buffer_nbytes(ctx, buffer);
    write.ptr = (uintptr_t)buffer->mirror;

    ret = ioctl(ctx->fd, HMM_DUMMY_WRITE, &write);
    if (ret) {
        fprintf(stderr, "(EE:%4d) %d\n", __LINE__, ret);
        hmm_exit();
    }

    buffer->nsys_pages = write.nsys_pages;
    buffer->nfaulted_sys_pages = write.nfaulted_sys_pages;

    return 0;
}

void hmm_buffer_free(struct hmm_ctx *ctx, struct hmm_buffer *buffer)
{
    if (ctx == NULL || buffer == NULL) {
        return;
    }

    munmap(buffer->ptr, buffer->npages << ctx->page_shift);
    if (buffer->fd >= 0) {
        close(buffer->fd);
    }
    free(buffer);
}

int main(int argc, const char *argv[])
{
    const struct hmm_test_result *result;
    struct hmm_ctx ctx;
    int ret;

    ctx.test_name = argv[0];
    ret = hmm_ctx_init(&ctx);
    if (ret) {
        goto out;
    }
    if (setjmp(_hmm_exit_env)) {
        ret = -1;
        goto out;
    }

    result = hmm_test(&ctx);
    ret = result ? result->ret : -1;
    hmm_ctx_fini(&ctx);

out:
    printf("(%s) %s\n", ret ? "EE" : "OK", ctx.test_name);
    return ret;
}
