/**
 * @file node_sine_v2.c
 * @brief Sine Wave Generator Node - Sequential Processing Version
 */

#include "audio_fw_v2.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define M_PI_F 3.14159265358979323846f

/**
 * @brief Private context for sine generator
 */
struct sine_ctx {
    float frequency;     /**< Frequency in Hz */
    float phase;         /**< Current phase (0 to 2π) */
    float phase_increment; /**< Phase increment per sample */
};

/**
 * @brief Sequential processing function for sine generator
 */
static struct audio_block* sine_process(struct audio_node *self, struct audio_block *in)
{
    struct sine_ctx *ctx = (struct sine_ctx *)self->ctx;

    // Generators ignore input and create their own block
    struct audio_block *out = audio_block_alloc();
    if (!out) {
        // If we can't allocate, release input and return NULL
        if (in) {
            audio_block_release(in);
        }
        return NULL;
    }

    // Generate sine wave
    for (size_t i = 0; i < out->data_len; i++) {
        float sample = sinf(ctx->phase) * INT16_MAX * 0.5f;  // 50% amplitude
        out->data[i] = (int16_t)sample;

        // Advance phase
        ctx->phase += ctx->phase_increment;
        if (ctx->phase >= 2.0f * M_PI_F) {
            ctx->phase -= 2.0f * M_PI_F;
        }
    }

    // Release input if present (generators don't use it)
    if (in) {
        audio_block_release(in);
    }

    return out;
}

/**
 * @brief Reset function
 */
static void sine_reset(struct audio_node *self)
{
    struct sine_ctx *ctx = (struct sine_ctx *)self->ctx;
    ctx->phase = 0.0f;
}

static const struct audio_node_api sine_api = {
    .process = sine_process,
    .reset = sine_reset,
};

static struct sine_ctx sine_contexts[4];  // Static allocation for up to 4 sine nodes
static size_t sine_ctx_index = 0;

void node_sine_init(struct audio_node *node, float freq)
{
    if (sine_ctx_index >= 4) {
        return;  // Error: too many sine nodes
    }

    struct sine_ctx *ctx = &sine_contexts[sine_ctx_index++];
    ctx->frequency = freq;
    ctx->phase = 0.0f;
    ctx->phase_increment = (2.0f * M_PI_F * freq) / CONFIG_AUDIO_SAMPLE_RATE;

    node->vtable = &sine_api;
    node->ctx = ctx;
}
