#include "audio_fw.h"
#include <zephyr/sys/printk.h>

/* Private Context: Kapselt die Lautstärke-Einstellung */
struct vol_ctx {
    float factor; // 1.0 = 100%, 0.5 = 50%
};

/* * Transform Node:
 * Liest Block -> Modifiziert Block -> Schiebt Block weiter
 */
void vol_process(struct audio_node *self) {
    struct vol_ctx *ctx = (struct vol_ctx *)self->ctx;

    // 1. Daten holen
    struct audio_block *block = k_fifo_get(&self->in_fifo, K_FOREVER);
    
    if (!block) return;

    // 2. Daten modifizieren (In-Place)
    // Achtung: Bei komplexeren Pipelines müsste man hier prüfen, 
    // ob der ref_count > 1 ist. Wenn ja, und wir modifizieren Daten, 
    // müssten wir den Block kopieren (Copy-on-Write), um andere Sinks nicht zu stören.
    // Für dieses MVP gehen wir davon aus, dass Filter VOR dem Splitter sitzen.
    
    for (size_t i = 0; i < block->data_len; i++) {
        // Simple Fixed-Point Logik oder FPU Nutzung
        float sample = (float)block->data[i];
        sample = sample * ctx->factor;
        
        // Hard-Clipping Schutz (verhindert Überlauf bei > 100% Volume)
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;

        block->data[i] = (int16_t)sample;
    }

    // 3. Weiterleiten an den nächsten Node
    // Die Hilfsfunktion kümmert sich um NULL-Checks beim Output
    audio_node_push_output(self, block);
}

const struct audio_node_api vol_api = {
    .process = vol_process,
    .reset = NULL
};

void node_vol_init(struct audio_node *node, float vol) {
    node->vtable = &vol_api;
    
    // Speicher für den Context allokieren
    struct vol_ctx *ctx = k_malloc(sizeof(struct vol_ctx));
    if (ctx) {
        ctx->factor = vol;
    }
    node->ctx = ctx;

    k_fifo_init(&node->in_fifo);
    // out_fifo wird später vom User (main.c) gesetzt
}
