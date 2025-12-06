#include "audio_fw.h"
#include <string.h>

#define MAX_SPLIT_OUTPUTS 4

struct splitter_ctx {
    struct k_fifo *outputs[MAX_SPLIT_OUTPUTS];
    int output_count;
};

void splitter_process(struct audio_node *self) {
    struct splitter_ctx *ctx = (struct splitter_ctx *)self->ctx;
    
    // 1. Wait for input
    struct audio_block *block = k_fifo_get(&self->in_fifo, K_FOREVER);
    
    if (ctx->output_count == 0) {
        audio_block_release(block);
        return;
    }

    // 2. Increase Ref Count
    // We already have 1 ref (from input). We need N refs total.
    // So we add (N - 1).
    if (ctx->output_count > 1) {
        atomic_add(&block->ref_count, ctx->output_count - 1);
    }

    // 3. Distribute Pointers (Zero Copy!)
    for (int i = 0; i < ctx->output_count; i++) {
        k_fifo_put(ctx->outputs[i], block);
    }
}

const struct audio_node_api splitter_api = { .process = splitter_process };

void node_splitter_init(struct audio_node *node) {
    node->vtable = &splitter_api;
    node->ctx = k_malloc(sizeof(struct splitter_ctx));
    memset(node->ctx, 0, sizeof(struct splitter_ctx));
    k_fifo_init(&node->in_fifo);
    node->out_fifo = NULL; // Splitter uses internal array
}

int node_splitter_add_output(struct audio_node *splitter, struct k_fifo *target_fifo) {
    struct splitter_ctx *ctx = (struct splitter_ctx *)splitter->ctx;
    if (ctx->output_count >= MAX_SPLIT_OUTPUTS) return -1;
    
    ctx->outputs[ctx->output_count++] = target_fifo;
    return 0;
}
