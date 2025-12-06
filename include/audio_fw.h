#ifndef AUDIO_FW_H
#define AUDIO_FW_H

#include <zephyr/kernel.h>
#include <stdint.h>

// --- Configuration Data Types ---
#define AUDIO_BLOCK_SIZE_BYTES  (CONFIG_AUDIO_BLOCK_SAMPLES * sizeof(int16_t))

// --- 1. The Data Container (Block) ---
struct audio_block {
    void *fifo_reserved;    // Required by Zephyr k_fifo
    int16_t *data;          // PCM Data Pointer
    size_t data_len;        // Number of valid samples
    atomic_t ref_count;     // Reference Counter (Atomic for thread safety)
};

// --- 2. The Node Interface (V-Table) ---
struct audio_node;

struct audio_node_api {
    void (*process)(struct audio_node *self); // The work loop
    void (*reset)(struct audio_node *self);   // Clear state (optional)
};

// --- 3. The Abstract Node Class ---
struct audio_node {
    const struct audio_node_api *vtable;
    struct k_fifo in_fifo;
    
    // Default Output (Simple chaining). 
    // Splitters might ignore this and use internal lists.
    struct k_fifo *out_fifo; 
    
    void *ctx; // Private Context (Encapsulation)
    
    // Threading Info
    struct k_thread thread_data;
    k_tid_t thread_id;
};

// --- 4. Public API: Memory Management ---
struct audio_block *audio_block_alloc(void);
void audio_block_release(struct audio_block *block);

// --- 5. Public API: Node Management ---
// Startet den Thread eines Nodes
void audio_node_start(struct audio_node *node, k_thread_stack_t *stack);

// Helper to push block to next node (handles NULL check & ref count logic)
void audio_node_push_output(struct audio_node *self, struct audio_block *block);


// --- 6. Node Constructors (Factories) ---
void node_sine_init(struct audio_node *node, float freq);
void node_vol_init(struct audio_node *node, float vol);
void node_log_sink_init(struct audio_node *node);

// Special: Splitter with N outputs
void node_splitter_init(struct audio_node *node);
int node_splitter_add_output(struct audio_node *splitter, struct k_fifo *target_fifo);

#endif // AUDIO_FW_H
