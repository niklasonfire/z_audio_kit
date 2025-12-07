#ifndef AUDIO_FW_H
#define AUDIO_FW_H

#include <zephyr/kernel.h>
#include <stdint.h>

/**
 * @brief Total size of an audio block in bytes.
 */
#define AUDIO_BLOCK_SIZE_BYTES  (CONFIG_AUDIO_BLOCK_SAMPLES * sizeof(int16_t))

/**
 * @brief Audio block structure holding PCM data.
 *
 * This structure acts as a wrapper around the raw PCM data buffer.
 * It includes reference counting to allow zero-copy distribution to multiple nodes.
 */
struct audio_block {
    void *fifo_reserved;    /**< Required by Zephyr k_fifo */
    int16_t *data;          /**< Pointer to PCM data buffer (allocated from slab) */
    size_t data_len;        /**< Number of valid samples in the buffer */
    atomic_t ref_count;     /**< Reference counter for memory management */
};

struct audio_node;

/**
 * @brief Interface for audio nodes.
 */
struct audio_node_api {
    /**
     * @brief Process function called in the node's thread loop.
     * @param self Pointer to the node instance.
     */
    void (*process)(struct audio_node *self);

    /**
     * @brief Reset function to clear internal state (optional).
     * @param self Pointer to the node instance.
     */
    void (*reset)(struct audio_node *self);
};

/**
 * @brief Abstract audio node structure.
 *
 * Represents a processing unit in the audio pipeline.
 */
struct audio_node {
    const struct audio_node_api *vtable; /**< Pointer to the implementation API */
    struct k_fifo in_fifo;               /**< Input FIFO queue for receiving blocks */
    struct k_fifo *out_fifo;             /**< Output FIFO queue for sending blocks (can be NULL) */
    void *ctx;                           /**< Private context data for the specific node implementation */
    struct k_thread thread_data;         /**< Thread data structure */
    k_tid_t thread_id;                   /**< Thread ID */
};

/**
 * @brief Allocates a new audio block.
 *
 * Allocates both the metadata wrapper and the data buffer from memory slabs.
 * Initialize the reference count to 1.
 *
 * @return Pointer to the allocated audio block, or NULL if allocation failed.
 */
struct audio_block *audio_block_alloc(void);

/**
 * @brief Releases a reference to an audio block.
 *
 * Decrements the reference count. If the count reaches zero, the memory
 * (both data buffer and wrapper) is freed back to the slabs.
 *
 * @param block Pointer to the audio block to release.
 */
void audio_block_release(struct audio_block *block);

/**
 * @brief Starts the processing thread for an audio node.
 *
 * @param node Pointer to the audio node.
 * @param stack Pointer to the stack memory area for the thread.
 */
void audio_node_start(struct audio_node *node, k_thread_stack_t *stack);

/**
 * @brief Pushes an audio block to the node's output.
 *
 * Handles NULL checks for the output FIFO. If no output is connected,
 * the block is released immediately.
 *
 * @param self Pointer to the current node.
 * @param block Pointer to the audio block to push.
 */
void audio_node_push_output(struct audio_node *self, struct audio_block *block);

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
 * @brief Initializes a logging sink node.
 *
 * This node logs peak values of received blocks and then releases them.
 *
 * @param node Pointer to the node structure to initialize.
 */
void node_log_sink_init(struct audio_node *node);

/**
 * @brief Initializes a splitter node.
 *
 * A splitter copies the input block pointer to multiple output FIFOs.
 *
 * @param node Pointer to the node structure to initialize.
 */
void node_splitter_init(struct audio_node *node);

/**
 * @brief Adds an output target to a splitter node.
 *
 * @param splitter Pointer to the splitter node.
 * @param target_fifo Pointer to the destination FIFO.
 * @return 0 on success, negative error code on failure (e.g., max outputs reached).
 */
int node_splitter_add_output(struct audio_node *splitter, struct k_fifo *target_fifo);

#endif // AUDIO_FW_H