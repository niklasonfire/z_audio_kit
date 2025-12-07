#include "audio_fw.h"
#include <math.h>

struct sine_ctx {
    float phase;
    float phase_inc;
    float amplitude;
};

void sine_process(struct audio_node *self) {
    struct sine_ctx *ctx = (struct sine_ctx *)self->ctx;

    struct audio_block *block = audio_block_alloc();
    
    if (!block) {
        k_sleep(K_MSEC(1)); 
        return;
    }

    for (int i = 0; i < CONFIG_AUDIO_BLOCK_SAMPLES; i++) {
        block->data[i] = (int16_t)(sinf(ctx->phase) * ctx->amplitude);
        
        ctx->phase += ctx->phase_inc;
        if (ctx->phase >= 6.28318f) ctx->phase -= 6.28318f;
    }

    audio_node_push_output(self, block);
    
    /* Simulate timing */
    int duration_us = (int)((uint64_t)CONFIG_AUDIO_BLOCK_SAMPLES * 1000000 / CONFIG_AUDIO_SAMPLE_RATE);
    k_sleep(K_USEC(duration_us));
}

const struct audio_node_api sine_api = { .process = sine_process };

void node_sine_init(struct audio_node *node, float freq) {
    node->vtable = &sine_api;
    
    struct sine_ctx *ctx = k_malloc(sizeof(struct sine_ctx));
    if (ctx) {
        ctx->phase = 0;
        ctx->amplitude = 10000.0f; 
        ctx->phase_inc = (2.0f * 3.14159f * freq) / CONFIG_AUDIO_SAMPLE_RATE;
        node->ctx = ctx;
    }
    
    k_fifo_init(&node->in_fifo);
}
