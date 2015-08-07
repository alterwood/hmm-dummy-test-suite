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
/*
 * This test case check that we can migrate anonymous memory to device memory
 * and write to it.
 */
#include "hmm_test_framework.h"

#define NPAGES  256

static struct hmm_test_result result;
struct hmm_ctx _ctx = {
    .test_name = "anon migration write test"
};

const struct hmm_test_result *hmm_test(struct hmm_ctx *ctx)
{
    struct hmm_buffer *buffer;
    unsigned i, size;
    int *ptr;

    HMM_BUFFER_NEW_ANON(buffer, NPAGES);
    size = hmm_buffer_nbytes(ctx, buffer);

    /* Migrate buffer to remote memory. */
    result.ret = 0;
    hmm_buffer_mirror_migrate_to(ctx, buffer);
    if (buffer->nfaulted_dev_pages != NPAGES) {
        fprintf(stderr, "(EE:%4d) migrated %ld pages out of %d\n",
                __LINE__, (long)buffer->nfaulted_dev_pages, NPAGES);
        result.ret = -1;
        goto out;
    }

    /* Initialize mirror buffer. */
    for (i = 0, ptr = buffer->mirror; i < size/sizeof(int); ++i) {
        ptr[i] = i;
    }

    /* Write buffer to its mirror using dummy driver. */
    hmm_buffer_mirror_write(ctx, buffer);
    if (buffer->ndev_pages != NPAGES) {
        fprintf(stderr, "(EE:%4d) write %ld pages out of %d\n",
                __LINE__, (long)buffer->ndev_pages, NPAGES);
        result.ret = -1;
        goto out;
    }

    /* Check value. */
    for (i = 0, ptr = buffer->ptr; i < size/sizeof(int); ++i) {
        if (ptr[i] != i) {
            fprintf(stderr, "(EE:%4d) invalid value [%d] got %d expected %d\n",
                    __LINE__, i, ptr[i], i);
            result.ret = -1;
            goto out;
        }
    }

out:
    hmm_buffer_free(ctx, buffer);

    return &result;
}
