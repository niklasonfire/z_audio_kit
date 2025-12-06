#include "audio_fw.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(audio_core, LOG_LEVEL_INF);

// Define Memory Slab for Audio Data
K_MEM_SLAB_DEFINE(audio_data_slab, AUDIO_BLOCK_SIZE_BYTES, CONFIG_AUDIO_MEM_SLAB_COUNT, 4);

struct audio_block *audio_block_alloc(void) {
    // 1. Allocate the metadata structure wrapper
    struct audio_block *block = k_malloc(sizeof(struct audio_block));
    if (!block) return NULL;

    // 2. Allocate the heavy PCM data buffer from Slab
    if (k_mem_slab_alloc(&audio_data_slab, (void **)&block->data, K_NO_WAIT) == 0) {
        memset(block->data, 0, AUDIO_BLOCK_SIZE_BYTES);
        block->data_len = CONFIG_AUDIO_BLOCK_SAMPLES;
        atomic_set(&block->ref_count, 1); // Init with 1 owner
        return block;
    }
    
    // Cleanup if slab alloc failed
    k_free(block);
    return NULL;
}

void audio_block_release(struct audio_block *block) {
    if (!block) return;

    /*
     * NOTE on Zephyr Atomic API:
     * atomic_dec() returns the PREVIOUS value (fetch_and_sub).
     *
     * Example Scenario (Two Owners):
     * 1. RefCount is 2.
     * 2. atomic_dec returns 2 (old value), and sets RefCount to 1.
     * 3. old_count (2) != 1. No free. Correct.
     *
     * Example Scenario (Last Owner):
     * 1. RefCount is 1.
     * 2. atomic_dec returns 1 (old value), and sets RefCount to 0.
     * 3. old_count (1) == 1. Free. Correct.
     *
     * To make this logic explicit and readable (addressing review comments),
     * we calculate the new_count locally and check against 0.
     */
    
    atomic_val_t old_count = atomic_dec(&block->ref_count);
    atomic_val_t new_count = old_count - 1;

    if (new_count == 0) { 
        // We were the last owner. The counter is now 0.
        // Free resources.
        k_mem_slab_free(&audio_data_slab, (void **)&block->data);
        k_free(block);
    }
}




// Thread Entry Point Wrapper
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
        // Dead end: Release block immediately
        audio_block_release(block);
    }
}
