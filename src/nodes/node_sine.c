#include "audio_fw.h"
#include <math.h>

struct sine_ctx {
    float phase;
    float phase_inc;
    float amplitude;
};

void sine_process(struct audio_node *self) {
    struct sine_ctx *ctx = (struct sine_ctx *)self->ctx;

    // 1. Block anfordern
    // Das Framework (core.c) garantiert jetzt, dass block->data
    // auf einen gültigen Slab-Speicher zeigt, wenn block != NULL ist.
    struct audio_block *block = audio_block_alloc();
    
    // 2. Fehlerbehandlung: Pool leer?
    if (!block) {
        // Wir warten kurz, damit andere Threads (Sink) Zeit haben,
        // Blöcke freizugeben.
        k_sleep(K_MSEC(1)); 
        return;
    }


    // 3. Audio generieren
    for (int i = 0; i < CONFIG_AUDIO_BLOCK_SAMPLES; i++) {
        block->data[i] = (int16_t)(sinf(ctx->phase) * ctx->amplitude);
        
        ctx->phase += ctx->phase_inc;
        if (ctx->phase >= 6.28318f) ctx->phase -= 6.28318f;
    }

    // 4. In die Pipeline schieben
    audio_node_push_output(self, block);
    
    // 5. Timing simulieren (Samples / Rate * 1000000 = µs)
    // Achtung: Integer-Überlauf vermeiden, daher nach u64 casten oder erst dividieren
    int duration_us = (int)((uint64_t)CONFIG_AUDIO_BLOCK_SAMPLES * 1000000 / CONFIG_AUDIO_SAMPLE_RATE);
    k_sleep(K_USEC(duration_us));
}

const struct audio_node_api sine_api = { .process = sine_process };

void node_sine_init(struct audio_node *node, float freq) {
    node->vtable = &sine_api;
    
    // Kontext initialisieren
    struct sine_ctx *ctx = k_malloc(sizeof(struct sine_ctx));
    if (ctx) {
        ctx->phase = 0;
        ctx->amplitude = 10000.0f; // Etwas leiser als Full-Scale (32767)
        ctx->phase_inc = (2.0f * 3.14159f * freq) / CONFIG_AUDIO_SAMPLE_RATE;
        node->ctx = ctx;
    }
    
    k_fifo_init(&node->in_fifo);
}