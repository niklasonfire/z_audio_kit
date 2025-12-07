#include "audio_fw.h"
#include <string.h>
#include <math.h>

struct analyzer_ctx {
    float smoothing;
    float current_rms_linear;
    struct analyzer_stats public_stats;
    struct k_spinlock lock;
};

/* Helper to calculate dBFS from linear amplitude (0.0 to 1.0) */
static float linear_to_db(float linear) {
    if (linear <= 0.00001f) return -100.0f; /* Floor at -100dB */
    return 20.0f * log10f(linear);
}

void analyzer_process(struct audio_node *self) {
    struct analyzer_ctx *ctx = (struct analyzer_ctx *)self->ctx;
    
    struct audio_block *block = k_fifo_get(&self->in_fifo, K_FOREVER);
    if (!block) return;

    /* Analysis Phase */
    float sum_sq = 0.0f;
    int16_t peak_abs = 0;
    bool clipped = false;

    if (block->data && block->data_len > 0) {
        for (size_t i = 0; i < block->data_len; i++) {
            int16_t val = block->data[i];
            
            /* Peak detection */
            int16_t abs_val = (val == -32768) ? 32767 : (val < 0 ? -val : val);
            if (abs_val > peak_abs) peak_abs = abs_val;
            
            /* Clipping detection */
            if (val == 32767 || val == -32768) clipped = true;

            /* RMS accumulation (normalized to 0..1 range first to avoid huge numbers) */
            float norm = (float)val / 32768.0f;
            sum_sq += norm * norm;
        }
        
        float rms_inst = sqrtf(sum_sq / block->data_len);

        /* Apply Smoothing (Leaky Integrator) */
        ctx->current_rms_linear = (ctx->current_rms_linear * ctx->smoothing) + 
                                  (rms_inst * (1.0f - ctx->smoothing));
    }

    /* Update Public Stats (Thread-Safe) */
    k_spinlock_key_t key = k_spin_lock(&ctx->lock);
    
    ctx->public_stats.rms_db = linear_to_db(ctx->current_rms_linear);
    ctx->public_stats.peak_db = linear_to_db((float)peak_abs / 32768.0f);
    if (clipped) ctx->public_stats.clipping = true; /* Sticky bit could be useful, but per-block is safer */
    else ctx->public_stats.clipping = false;

    k_spin_unlock(&ctx->lock, key);

    /* Pass-Through: Send to output or release */
    audio_node_push_output(self, block);
}

const struct audio_node_api analyzer_api = {
    .process = analyzer_process,
    .reset = NULL
};

void node_analyzer_init(struct audio_node *node, float smoothing_factor) {
    node->vtable = &analyzer_api;
    
    struct analyzer_ctx *ctx = k_malloc(sizeof(struct analyzer_ctx));
    if (ctx) {
        ctx->smoothing = smoothing_factor;
        ctx->current_rms_linear = 0.0f;
        memset(&ctx->public_stats, 0, sizeof(struct analyzer_stats));
        ctx->public_stats.rms_db = -100.0f;
        ctx->public_stats.peak_db = -100.0f;
    }
    node->ctx = ctx;

    k_fifo_init(&node->in_fifo);
    node->out_fifo = NULL;
}

int node_analyzer_get_stats(struct audio_node *node, struct analyzer_stats *stats) {
    if (!node || !node->ctx || !stats) return -1;
    
    struct analyzer_ctx *ctx = (struct analyzer_ctx *)node->ctx;
    
    k_spinlock_key_t key = k_spin_lock(&ctx->lock);
    memcpy(stats, &ctx->public_stats, sizeof(struct analyzer_stats));
    k_spin_unlock(&ctx->lock, key);
    
    return 0;
}
