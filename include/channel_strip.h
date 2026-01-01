#ifndef CHANNEL_STRIP_H
#define CHANNEL_STRIP_H

#include "audio_fw_v2.h"
#include <zephyr/kernel.h>

/**
 * @file channel_strip.h
 * @brief Channel Strip Architecture for Deterministic Audio Processing
 *
 * A channel strip is a container for a sequential chain of audio nodes,
 * analogous to a channel strip on a mixing console (e.g., Input → EQ → Comp → Gate → Fader).
 *
 * Key features:
 * - Deterministic latency: All nodes process sequentially in one thread
 * - Low jitter: No inter-node context switching
 * - Synchronized: Multiple strips can process in lockstep
 * - Order preservation: Processing order matches array order
 */

#define CHANNEL_STRIP_MAX_NODES  16  /**< Maximum nodes per channel strip */

/**
 * @brief Channel strip structure.
 *
 * Represents a sequential processing chain with managed threading.
 */
struct channel_strip {
    /** @brief Array of node pointers in processing order */
    struct audio_node *nodes[CHANNEL_STRIP_MAX_NODES];

    /** @brief Number of active nodes in the chain */
    size_t node_count;

    /** @brief Input FIFO for receiving blocks from external sources */
    struct k_fifo in_fifo;

    /** @brief Output FIFO for sending processed blocks (optional) */
    struct k_fifo *out_fifo;

    /** @brief Thread data for this strip's processing thread */
    struct k_thread thread_data;

    /** @brief Thread ID */
    k_tid_t thread_id;

    /** @brief User-defined name for debugging */
    const char *name;
};

/**
 * @brief Initializes a channel strip.
 *
 * @param strip Pointer to the channel strip structure
 * @param name Debug name for the strip (e.g., "Channel 1")
 */
void channel_strip_init(struct channel_strip *strip, const char *name);

/**
 * @brief Adds a node to the end of the processing chain.
 *
 * Nodes are processed in the order they are added.
 *
 * @param strip Pointer to the channel strip
 * @param node Pointer to the node to add
 * @return 0 on success, -ENOMEM if strip is full
 */
int channel_strip_add_node(struct channel_strip *strip, struct audio_node *node);

/**
 * @brief Removes all nodes from the strip.
 *
 * @param strip Pointer to the channel strip
 */
void channel_strip_clear(struct channel_strip *strip);

/**
 * @brief Starts the channel strip processing thread.
 *
 * The thread will block on the input FIFO, process blocks through all nodes
 * sequentially, and push to the output FIFO.
 *
 * @param strip Pointer to the channel strip
 * @param stack Pointer to the stack memory for the thread
 * @param stack_size Size of the stack in bytes
 * @param priority Thread priority (lower value = higher priority)
 */
void channel_strip_start(struct channel_strip *strip,
                          k_thread_stack_t *stack,
                          size_t stack_size,
                          int priority);

/**
 * @brief Stops the channel strip processing thread.
 *
 * @param strip Pointer to the channel strip
 */
void channel_strip_stop(struct channel_strip *strip);

/**
 * @brief Processes a single block through the channel strip (non-threaded).
 *
 * This is the core sequential processing function. It can be called:
 * - From the strip's own thread (in channel_strip_start)
 * - From an external callback (ISR, custom thread)
 * - Directly for testing
 *
 * @param strip Pointer to the channel strip
 * @param block Input block
 * @return Output block after processing through all nodes
 */
struct audio_block* channel_strip_process_block(struct channel_strip *strip,
                                                 struct audio_block *block);

/**
 * @brief Pushes a block to the strip's input FIFO.
 *
 * Convenience function for external producers.
 *
 * @param strip Pointer to the channel strip
 * @param block Block to push
 */
void channel_strip_push_input(struct channel_strip *strip, struct audio_block *block);

// ============================================================================
// Mixer Architecture (Multiple Synchronized Strips)
// ============================================================================

#define MIXER_MAX_CHANNELS  32  /**< Maximum number of channels in a mixer */

/**
 * @brief Mixer structure for managing multiple channel strips.
 *
 * Provides synchronized processing of multiple channels, ensuring all
 * channels process the same sample index at the same time.
 */
struct audio_mixer {
    /** @brief Array of channel strips */
    struct channel_strip *channels[MIXER_MAX_CHANNELS];

    /** @brief Number of active channels */
    size_t channel_count;

    /** @brief Master channel strip (optional) */
    struct channel_strip *master;

    /** @brief Input FIFO for receiving multi-channel blocks */
    struct k_fifo in_fifo;

    /** @brief Output FIFO for sending mixed blocks */
    struct k_fifo *out_fifo;

    /** @brief Thread data for synchronized processing */
    struct k_thread thread_data;

    /** @brief Thread ID */
    k_tid_t thread_id;
};

/**
 * @brief Initializes a mixer.
 *
 * @param mixer Pointer to the mixer structure
 */
void audio_mixer_init(struct audio_mixer *mixer);

/**
 * @brief Adds a channel strip to the mixer.
 *
 * @param mixer Pointer to the mixer
 * @param strip Pointer to the channel strip to add
 * @return Channel index, or negative error code
 */
int audio_mixer_add_channel(struct audio_mixer *mixer, struct channel_strip *strip);

/**
 * @brief Sets the master bus strip.
 *
 * @param mixer Pointer to the mixer
 * @param master Pointer to the master channel strip
 */
void audio_mixer_set_master(struct audio_mixer *mixer, struct channel_strip *master);

/**
 * @brief Starts the mixer's synchronized processing thread.
 *
 * All channels process blocks in lockstep for deterministic timing.
 *
 * @param mixer Pointer to the mixer
 * @param stack Pointer to the stack memory
 * @param stack_size Size of the stack in bytes
 * @param priority Thread priority
 */
void audio_mixer_start(struct audio_mixer *mixer,
                       k_thread_stack_t *stack,
                       size_t stack_size,
                       int priority);

/**
 * @brief Process a block through all channels synchronously (non-threaded).
 *
 * Each channel processes the same block sequentially, results are summed,
 * then passed through the master strip.
 *
 * @param mixer Pointer to the mixer
 * @param block Input block
 * @return Output block after mixing
 */
struct audio_block* audio_mixer_process_block(struct audio_mixer *mixer,
                                               struct audio_block *block);

#endif // CHANNEL_STRIP_H
