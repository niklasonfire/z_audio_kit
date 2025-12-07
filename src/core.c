#include "audio_fw.h"
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(audio_core, LOG_LEVEL_INF);

K_MEM_SLAB_DEFINE(audio_data_slab, AUDIO_BLOCK_SIZE_BYTES, CONFIG_AUDIO_MEM_SLAB_COUNT, 4);
K_MEM_SLAB_DEFINE(audio_block_slab, sizeof(struct audio_block), CONFIG_AUDIO_MEM_SLAB_COUNT, 4);

struct audio_block *audio_block_alloc(void) {
    struct audio_block *block;

    if (k_mem_slab_alloc(&audio_block_slab, (void **)&block, K_NO_WAIT) != 0) {
        return NULL;
    }

    if (k_mem_slab_alloc(&audio_data_slab, (void **)&block->data, K_NO_WAIT) == 0) {
        memset(block->data, 0, AUDIO_BLOCK_SIZE_BYTES);
        block->data_len = CONFIG_AUDIO_BLOCK_SAMPLES;
        atomic_set(&block->ref_count, 1);
        return block;
    }
    
    k_mem_slab_free(&audio_block_slab, (void *)block);
    return NULL;
}

void audio_block_release(struct audio_block *block) {
    if (!block) return;

    atomic_val_t old_count = atomic_dec(&block->ref_count);
    atomic_val_t new_count = old_count - 1;

    if (new_count == 0) { 
        if (block->data != NULL) {
            k_mem_slab_free(&audio_data_slab, (void *)block->data);
            block->data = NULL;
        }
        k_mem_slab_free(&audio_block_slab, (void *)block); 
    }
}

int audio_block_get_writable(struct audio_block **block_ptr) {
    struct audio_block *block = *block_ptr;
    if (!block) return -EINVAL;

    if (atomic_get(&block->ref_count) > 1) {
        struct audio_block *new_block = audio_block_alloc();
        if (!new_block) {
            LOG_WRN("CoW failed: OOM (original ref_count=%ld)", atomic_get(&block->ref_count));
            return -ENOMEM;
        }

        LOG_DBG("CoW executed: %p -> %p", block, new_block);

        memcpy(new_block->data, block->data, AUDIO_BLOCK_SIZE_BYTES);
        new_block->data_len = block->data_len;

        audio_block_release(block);
        *block_ptr = new_block;
    }
    return 0;
}

static void node_thread_entry(void *p1, void *p2, void *p3) {
    struct audio_node *node = (struct audio_node *)p1;
    if (node->vtable && node->vtable->process) {
        while (1) {
            node->vtable->process(node);
        }
    }
}

void audio_node_start(struct audio_node *node, k_thread_stack_t *stack) {
    node->thread_id = k_thread_create(&node->thread_data, stack, 
                    CONFIG_AUDIO_THREAD_STACK_SIZE,
                    node_thread_entry, node, NULL, NULL,
                    CONFIG_AUDIO_THREAD_PRIORITY, 0, K_NO_WAIT);
}

void audio_node_push_output(struct audio_node *self, struct audio_block *block) {
    if (self->out_fifo) {
        k_fifo_put(self->out_fifo, block);
    } else {
        audio_block_release(block);
    }
}