/**
 * @file channel_strip.c
 * @brief Channel Strip Implementation
 */

#include "channel_strip.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(channel_strip, LOG_LEVEL_INF);

// ============================================================================
// Channel Strip Implementation
// ============================================================================

void channel_strip_init(struct channel_strip *strip, const char *name)
{
    strip->node_count = 0;
    strip->out_fifo = NULL;
    strip->thread_id = NULL;
    strip->name = name ? name : "Unnamed";
    k_fifo_init(&strip->in_fifo);

    // Clear node array
    for (size_t i = 0; i < CHANNEL_STRIP_MAX_NODES; i++) {
        strip->nodes[i] = NULL;
    }
}

int channel_strip_add_node(struct channel_strip *strip, struct audio_node *node)
{
    if (strip->node_count >= CHANNEL_STRIP_MAX_NODES) {
        return -ENOMEM;
    }

    strip->nodes[strip->node_count++] = node;
    return 0;
}

void channel_strip_clear(struct channel_strip *strip)
{
    strip->node_count = 0;
    for (size_t i = 0; i < CHANNEL_STRIP_MAX_NODES; i++) {
        strip->nodes[i] = NULL;
    }
}

struct audio_block* channel_strip_process_block(struct channel_strip *strip,
                                                 struct audio_block *block)
{
    if (!block) {
        return NULL;
    }

    // Sequential processing through all nodes
    for (size_t i = 0; i < strip->node_count; i++) {
        block = audio_node_process(strip->nodes[i], block);

        // If a node returns NULL, it's dropping the block (e.g., gate/mute)
        if (!block) {
            return NULL;
        }
    }

    return block;
}

/**
 * @brief Thread entry point for channel strip processing.
 */
static void channel_strip_thread_entry(void *p1, void *p2, void *p3)
{
    struct channel_strip *strip = (struct channel_strip *)p1;

    LOG_INF("Channel strip '%s' thread started", strip->name);

    while (1) {
        // Block waiting for input
        struct audio_block *block = k_fifo_get(&strip->in_fifo, K_FOREVER);

        // Process through all nodes sequentially
        block = channel_strip_process_block(strip, block);

        // Push to output or release
        if (block) {
            if (strip->out_fifo) {
                k_fifo_put(strip->out_fifo, block);
            } else {
                audio_block_release(block);
            }
        }
    }
}

void channel_strip_start(struct channel_strip *strip,
                          k_thread_stack_t *stack,
                          size_t stack_size,
                          int priority)
{
    strip->thread_id = k_thread_create(&strip->thread_data,
                                       stack,
                                       stack_size,
                                       channel_strip_thread_entry,
                                       strip, NULL, NULL,
                                       priority,
                                       0,
                                       K_NO_WAIT);

    // Set thread name for debugging
    char thread_name[16];
    snprintf(thread_name, sizeof(thread_name), "strip_%s", strip->name);
    k_thread_name_set(strip->thread_id, thread_name);
}

void channel_strip_stop(struct channel_strip *strip)
{
    if (strip->thread_id) {
        k_thread_abort(strip->thread_id);
        strip->thread_id = NULL;
    }
}

void channel_strip_push_input(struct channel_strip *strip, struct audio_block *block)
{
    k_fifo_put(&strip->in_fifo, block);
}

// ============================================================================
// Mixer Implementation
// ============================================================================

void audio_mixer_init(struct audio_mixer *mixer)
{
    mixer->channel_count = 0;
    mixer->master = NULL;
    mixer->out_fifo = NULL;
    mixer->thread_id = NULL;
    k_fifo_init(&mixer->in_fifo);

    for (size_t i = 0; i < MIXER_MAX_CHANNELS; i++) {
        mixer->channels[i] = NULL;
    }
}

int audio_mixer_add_channel(struct audio_mixer *mixer, struct channel_strip *strip)
{
    if (mixer->channel_count >= MIXER_MAX_CHANNELS) {
        return -ENOMEM;
    }

    mixer->channels[mixer->channel_count] = strip;
    return mixer->channel_count++;
}

void audio_mixer_set_master(struct audio_mixer *mixer, struct channel_strip *master)
{
    mixer->master = master;
}

struct audio_block* audio_mixer_process_block(struct audio_mixer *mixer,
                                               struct audio_block *block)
{
    if (!block || mixer->channel_count == 0) {
        return block;
    }

    // Allocate a mix buffer for summing
    struct audio_block *mix_block = audio_block_alloc();
    if (!mix_block) {
        audio_block_release(block);
        return NULL;
    }

    // Zero the mix buffer
    for (size_t i = 0; i < mix_block->data_len; i++) {
        mix_block->data[i] = 0;
    }

    // Process each channel and sum results
    for (size_t ch = 0; ch < mixer->channel_count; ch++) {
        // Create a copy for this channel (each channel needs its own block)
        struct audio_block *ch_block = audio_block_alloc();
        if (!ch_block) {
            continue;  // Skip this channel on allocation failure
        }

        // Copy input data
        for (size_t i = 0; i < block->data_len; i++) {
            ch_block->data[i] = block->data[i];
        }
        ch_block->data_len = block->data_len;

        // Process through channel strip
        ch_block = channel_strip_process_block(mixer->channels[ch], ch_block);

        // Sum into mix buffer
        if (ch_block) {
            for (size_t i = 0; i < ch_block->data_len && i < mix_block->data_len; i++) {
                int32_t sum = (int32_t)mix_block->data[i] + (int32_t)ch_block->data[i];
                // Simple clipping
                if (sum > INT16_MAX) sum = INT16_MAX;
                if (sum < INT16_MIN) sum = INT16_MIN;
                mix_block->data[i] = (int16_t)sum;
            }
            audio_block_release(ch_block);
        }
    }

    // Release original input block
    audio_block_release(block);

    // Process through master bus if present
    if (mixer->master) {
        mix_block = channel_strip_process_block(mixer->master, mix_block);
    }

    return mix_block;
}

/**
 * @brief Thread entry point for mixer processing.
 */
static void audio_mixer_thread_entry(void *p1, void *p2, void *p3)
{
    struct audio_mixer *mixer = (struct audio_mixer *)p1;

    LOG_INF("Mixer thread started with %zu channels", mixer->channel_count);

    while (1) {
        // Block waiting for input
        struct audio_block *block = k_fifo_get(&mixer->in_fifo, K_FOREVER);

        // Process through all channels in lockstep
        block = audio_mixer_process_block(mixer, block);

        // Push to output or release
        if (block) {
            if (mixer->out_fifo) {
                k_fifo_put(mixer->out_fifo, block);
            } else {
                audio_block_release(block);
            }
        }
    }
}

void audio_mixer_start(struct audio_mixer *mixer,
                       k_thread_stack_t *stack,
                       size_t stack_size,
                       int priority)
{
    mixer->thread_id = k_thread_create(&mixer->thread_data,
                                       stack,
                                       stack_size,
                                       audio_mixer_thread_entry,
                                       mixer, NULL, NULL,
                                       priority,
                                       0,
                                       K_NO_WAIT);

    k_thread_name_set(mixer->thread_id, "audio_mixer");
}
