#include "audio_fw.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(audio_sink, LOG_LEVEL_INF);

void log_sink_process(struct audio_node *self) {
    struct audio_block *block = k_fifo_get(&self->in_fifo, K_FOREVER);
    if (!block) return;

    int16_t max_val = 0;
    if (block->data) {
        for(size_t i = 0; i < block->data_len; i++) {
            int16_t val = block->data[i];
            if (val < 0) val = -val;
            if (val > max_val) max_val = val;
        }
    }
    LOG_INF("SINK [%p]: Peak=%d | RefCount=%ld", block, max_val, atomic_get(&block->ref_count));

    audio_block_release(block);
}

const struct audio_node_api log_sink_api = {
    .process = log_sink_process,
    .reset = NULL
};

void node_log_sink_init(struct audio_node *node) {
    node->vtable = &log_sink_api;
    node->ctx = NULL;
    
    k_fifo_init(&node->in_fifo);
    node->out_fifo = NULL;
}