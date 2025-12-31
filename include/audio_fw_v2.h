#ifndef AUDIO_FW_V2_H
#define AUDIO_FW_V2_H

#include <zephyr/kernel.h>
#include <stdint.h>

/**
 * @file audio_fw_v2.h
 * @brief Sequential Audio Processing Framework
 *
 * This version removes threading from individual nodes, making them pure
 * processing units. Threading is handled externally by:
 * - Channel strips (managed threading for deterministic processing)
 * - User code (manual threading for custom scenarios)
 */

/**
 * @brief Total size of an audio block in bytes.
 */
#define AUDIO_BLOCK_SIZE_BYTES  (CONFIG_AUDIO_BLOCK_SAMPLES * sizeof(int16_t))

/**
 * @brief Audio block structure holding PCM data.
 *
 * Simplified version without reference counting for sequential processing.
 * In sequential mode, blocks flow through the pipeline without sharing.
 */
struct audio_block {
    int16_t *data;          /**< Pointer to PCM data buffer */
    size_t data_len;        /**< Number of valid samples in the buffer */
};

/**
 * @brief Forward declaration
 */
struct audio_node;

/**
 * @brief Sequential processing API for audio nodes.
 *
 * Nodes are now pure processing functions - no internal threading.
 */
struct audio_node_api {
    /**
     * @brief Process an audio block sequentially.
     *
     * The node processes the input block and returns a block to pass
     * to the next node. The node may:
     * - Modify the input block in-place and return it (transforms)
     * - Return a different block (generators, effects with internal buffers)
     * - Return NULL to drop the block (gates, mutes)
     *
     * @param self Pointer to the node instance
     * @param in Input block (may be NULL for generator nodes)
     * @return Output block to pass to next node, or NULL to drop
     */
    struct audio_block* (*process)(struct audio_node *self, struct audio_block *in);

    /**
     * @brief Reset function to clear internal state (optional).
     * @param self Pointer to the node instance.
     */
    void (*reset)(struct audio_node *self);
};

/**
 * @brief Simplified audio node structure.
 *
 * Represents a pure processing unit - no FIFOs, no threads.
 * Threading is managed externally.
 */
struct audio_node {
    const struct audio_node_api *vtable; /**< Pointer to the implementation API */
    void *ctx;                           /**< Private context data for the node */
};

/**
 * @brief Allocates a new audio block.
 *
 * @return Pointer to the allocated audio block, or NULL if allocation failed.
 */
struct audio_block *audio_block_alloc(void);

/**
 * @brief Releases an audio block back to the memory pool.
 *
 * @param block Pointer to the audio block to release.
 */
void audio_block_release(struct audio_block *block);

/**
 * @brief Process a block through a node (convenience wrapper).
 *
 * @param node The node to process through
 * @param in Input block
 * @return Output block from the node
 */
static inline struct audio_block* audio_node_process(struct audio_node *node,
                                                       struct audio_block *in)
{
    if (node && node->vtable && node->vtable->process) {
        return node->vtable->process(node, in);
    }
    return in;
}

/**
 * @brief Reset a node's internal state (convenience wrapper).
 *
 * @param node The node to reset
 */
static inline void audio_node_reset(struct audio_node *node)
{
    if (node && node->vtable && node->vtable->reset) {
        node->vtable->reset(node);
    }
}

// ============================================================================
// Node Initialization Functions
// ============================================================================

/**
 * @brief Initializes a sine wave generator node.
 *
 * @param node Pointer to the node structure to initialize.
 * @param freq Frequency of the sine wave in Hz.
 */
void node_sine_init(struct audio_node *node, float freq);

/**
 * @brief Initializes a volume control node.
 *
 * @param node Pointer to the node structure to initialize.
 * @param vol Volume factor (e.g., 1.0 for 100%, 0.5 for 50%).
 */
void node_vol_init(struct audio_node *node, float vol);

/**
 * @brief Updates the volume of a volume node.
 *
 * @param node Pointer to the volume node.
 * @param vol New volume factor.
 */
void node_vol_set(struct audio_node *node, float vol);

/**
 * @brief Initializes a logging sink node.
 *
 * This node logs peak values of received blocks.
 *
 * @param node Pointer to the node structure to initialize.
 */
void node_log_sink_init(struct audio_node *node);

/**
 * @brief Statistics structure for the Analyzer Node.
 */
struct analyzer_stats {
    float rms_db;       /**< RMS level in dBFS (e.g., -20.0 to 0.0) */
    float peak_db;      /**< Peak level in dBFS */
    bool clipping;      /**< True if any sample hit min/max range */
};

/**
 * @brief Initializes an analyzer (metering) node.
 *
 * This is a pass-through node that calculates statistics on the audio stream
 * without modifying it.
 *
 * @param node Pointer to the node structure.
 * @param smoothing_factor Value between 0.0 (no smoothing) and 0.99 (heavy smoothing).
 */
void node_analyzer_init(struct audio_node *node, float smoothing_factor);

/**
 * @brief Retrieves the latest statistics from the analyzer.
 *
 * @param node Pointer to the analyzer node.
 * @param stats Pointer to the destination struct to write statistics into.
 * @return 0 on success, negative on error.
 */
int node_analyzer_get_stats(struct audio_node *node, struct analyzer_stats *stats);

// ============================================================================
// Spectrum Analyzer Node (Large Window Example)
// ============================================================================

/**
 * @brief Initializes a spectrum analyzer node.
 *
 * This node demonstrates processing with large windows (>128 samples).
 * It accumulates samples until buffer is full, then computes FFT.
 *
 * @param node Pointer to the node structure.
 * @param fft_size FFT size in samples (must be power of 2, max 1024).
 */
void node_spectrum_analyzer_init(struct audio_node *node, size_t fft_size);

/**
 * @brief Get the magnitude spectrum.
 *
 * @param node Pointer to the spectrum analyzer node.
 * @param spectrum_out Output buffer for magnitude values.
 * @param out_size Size of output buffer (typically fft_size/2).
 * @return 0 on success, -EAGAIN if not ready, -EINVAL on error.
 */
int node_spectrum_analyzer_get_spectrum(struct audio_node *node,
                                        float *spectrum_out,
                                        size_t out_size);

/**
 * @brief Get the spectrum in dB scale.
 *
 * @param node Pointer to the spectrum analyzer node.
 * @param spectrum_db_out Output buffer for dB values.
 * @param out_size Size of output buffer.
 * @param reference Reference level for dB calculation (typically 1.0).
 * @return 0 on success, -EAGAIN if not ready, -EINVAL on error.
 */
int node_spectrum_analyzer_get_spectrum_db(struct audio_node *node,
                                           float *spectrum_db_out,
                                           size_t out_size,
                                           float reference);

/**
 * @brief Convert bin index to frequency in Hz.
 *
 * @param bin_index Frequency bin index.
 * @param fft_size FFT size used.
 * @param sample_rate Sample rate in Hz.
 * @return Frequency in Hz.
 */
float spectrum_analyzer_bin_to_freq(size_t bin_index,
                                    size_t fft_size,
                                    uint32_t sample_rate);

/**
 * @brief Get number of FFTs computed.
 *
 * @param node Pointer to the spectrum analyzer node.
 * @return Number of FFT computations performed.
 */
uint32_t node_spectrum_analyzer_get_process_count(struct audio_node *node);

#endif // AUDIO_FW_V2_H
