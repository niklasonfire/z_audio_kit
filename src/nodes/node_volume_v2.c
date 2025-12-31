/**
 * @file node_volume_v2.c
 * @brief Volume Control Node - Sequential Processing Version
 */

#include "audio_fw_v2.h"
#include <string.h>

/**
 * @brief Private context for volume node
 */
struct volume_ctx {
    float factor;  /**< Volume multiplication factor */
};

/**
 * @brief Sequential processing function for volume node
 */
static struct audio_block* vol_process(struct audio_node *self, struct audio_block *in)
{
    if (!in) {
        return NULL;  // Volume node requires input
    }

    struct volume_ctx *ctx = (struct volume_ctx *)self->ctx;

    // Modify samples in-place (no CoW needed in sequential mode)
    for (size_t i = 0; i < in->data_len; i++) {
        float sample = (float)in->data[i];
        sample = sample * ctx->factor;

        // Clipping
        if (sample > INT16_MAX) sample = INT16_MAX;
        if (sample < INT16_MIN) sample = INT16_MIN;

        in->data[i] = (int16_t)sample;
    }

    return in;  // Return modified block
}

/**
 * @brief Reset function (nothing to reset for stateless volume)
 */
static void vol_reset(struct audio_node *self)
{
    // Volume node is stateless, nothing to reset
}

static const struct audio_node_api volume_api = {
    .process = vol_process,
    .reset = vol_reset,
};

static struct volume_ctx volume_contexts[8];  // Static allocation for up to 8 volume nodes
static size_t volume_ctx_index = 0;

void node_vol_init(struct audio_node *node, float vol)
{
    // Allocate context
    if (volume_ctx_index >= 8) {
        // Error: too many volume nodes
        return;
    }

    struct volume_ctx *ctx = &volume_contexts[volume_ctx_index++];
    ctx->factor = vol;

    node->vtable = &volume_api;
    node->ctx = ctx;
}

void node_vol_set(struct audio_node *node, float vol)
{
    struct volume_ctx *ctx = (struct volume_ctx *)node->ctx;
    if (ctx) {
        ctx->factor = vol;
    }
}
