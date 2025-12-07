#include "audio_fw.h"
#include <zephyr/sys/printk.h>
#include <string.h>

struct vol_ctx {
    float factor;
};

void vol_process(struct audio_node *self) {
    struct vol_ctx *ctx = (struct vol_ctx *)self->ctx;

    struct audio_block *block = k_fifo_get(&self->in_fifo, K_FOREVER);
    
    if (!block) return;

    /* Copy-on-Write: If block is shared, duplicate it before modification */
    if (atomic_get(&block->ref_count) > 1) {
        struct audio_block *new_block = audio_block_alloc();
        if (!new_block) {
            /* Allocation failed; drop the frame */
            audio_block_release(block); 
            return;
        }
        
        memcpy(new_block->data, block->data, AUDIO_BLOCK_SIZE_BYTES);
        new_block->data_len = block->data_len;

        audio_block_release(block);
        block = new_block;
    }
    
    for (size_t i = 0; i < block->data_len; i++) {
        float sample = (float)block->data[i];
        sample = sample * ctx->factor;
        
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;

        block->data[i] = (int16_t)sample;
    }

    audio_node_push_output(self, block);
}

const struct audio_node_api vol_api = {
    .process = vol_process,
    .reset = NULL
};

void node_vol_init(struct audio_node *node, float vol) {
    node->vtable = &vol_api;
    
    struct vol_ctx *ctx = k_malloc(sizeof(struct vol_ctx));
    if (ctx) {
        ctx->factor = vol;
    }
    node->ctx = ctx;

    k_fifo_init(&node->in_fifo);
}