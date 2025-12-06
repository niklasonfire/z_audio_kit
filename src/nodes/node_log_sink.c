#include "audio_fw.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(audio_sink, LOG_LEVEL_INF);

/* * Consumer Node: 
 * Liest Daten, macht etwas damit (Logging) und zerstört den Block.
 */
void log_sink_process(struct audio_node *self) {
    struct audio_block *block = k_fifo_get(&self->in_fifo, K_FOREVER);
    if (!block) return;

    // Verarbeitung...
    int16_t max_val = 0;
    if (block->data) {
        for(size_t i = 0; i < block->data_len; i++) {
            int16_t val = block->data[i];
            if (val < 0) val = -val;
            if (val > max_val) max_val = val;
        }
    }
    LOG_INF("SINK [%p]: Peak=%d | RefCount=%d", block, max_val, atomic_get(&block->ref_count));

    // --- ÄNDERUNG: KEIN k_free MEHR! ---
    // Das Framework besitzt diesen Pointer (Slab) und audio_block_release räumt ihn auf.
    
    // 3. Container und Daten zurückgeben
    audio_block_release(block);
}

const struct audio_node_api log_sink_api = {
    .process = log_sink_process,
    .reset = NULL
};

void node_log_sink_init(struct audio_node *node) {
    node->vtable = &log_sink_api;
    node->ctx = NULL; // Kein interner Zustand nötig
    
    k_fifo_init(&node->in_fifo);
    node->out_fifo = NULL; // Sinks haben keinen Ausgang
}
