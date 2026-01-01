/**
 * @file example_standalone_nodes.c
 * @brief Example: Using Nodes Standalone with Custom Threading
 *
 * This example shows how to use nodes OUTSIDE of channel strips,
 * where the user manages threading manually.
 *
 * Use cases:
 * - Custom processing architectures
 * - Integration with existing threading models
 * - Testing and prototyping
 * - Non-real-time processing (batch processing)
 */

#include "audio_fw_v2.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(standalone, LOG_LEVEL_INF);

// ============================================================================
// Example 1: Simple Sequential Processing (No Threading)
// ============================================================================

void example_simple_sequential(void)
{
    LOG_INF("=== Example: Simple Sequential Processing ===");

    // Create nodes
    struct audio_node generator, volume, analyzer;

    node_sine_init(&generator, 440.0f);
    node_vol_init(&volume, 0.7f);
    node_analyzer_init(&analyzer, 0.9f);

    // Process 10 blocks sequentially
    for (int i = 0; i < 10; i++) {
        // Generate block
        struct audio_block *block = audio_node_process(&generator, NULL);

        // Apply volume
        block = audio_node_process(&volume, block);

        // Analyze
        block = audio_node_process(&analyzer, block);

        // Get stats
        struct analyzer_stats stats;
        node_analyzer_get_stats(&analyzer, &stats);
        LOG_INF("Block %d: RMS=%.1f dB, Peak=%.1f dB", i, stats.rms_db, stats.peak_db);

        // Release
        audio_block_release(block);

        k_sleep(K_MSEC(100));
    }
}

// ============================================================================
// Example 2: Custom Producer Thread
// ============================================================================

struct producer_context {
    struct audio_node *generator;
    struct k_fifo *output_fifo;
};

static void producer_thread(void *p1, void *p2, void *p3)
{
    struct producer_context *ctx = (struct producer_context *)p1;

    LOG_INF("Producer thread started");

    while (1) {
        // Generate a block
        struct audio_block *block = audio_node_process(ctx->generator, NULL);

        if (block) {
            // Push to output FIFO for consumer
            k_fifo_put(ctx->output_fifo, block);
        }

        // Rate limiting (generate ~100 blocks/sec)
        k_sleep(K_MSEC(10));
    }
}

// ============================================================================
// Example 3: Custom Consumer Thread
// ============================================================================

struct consumer_context {
    struct audio_node *processor;
    struct k_fifo *input_fifo;
};

static void consumer_thread(void *p1, void *p2, void *p3)
{
    struct consumer_context *ctx = (struct consumer_context *)p1;

    LOG_INF("Consumer thread started");

    while (1) {
        // Wait for block from producer
        struct audio_block *block = k_fifo_get(ctx->input_fifo, K_FOREVER);

        // Process through node
        block = audio_node_process(ctx->processor, block);

        // Do something with result (e.g., log, output to DAC, etc.)
        if (block) {
            LOG_INF("Consumer processed block with %zu samples", block->data_len);
            audio_block_release(block);
        }
    }
}

void example_custom_threading(void)
{
    LOG_INF("=== Example: Custom Producer/Consumer Threading ===");

    // Create nodes
    static struct audio_node generator, volume;
    node_sine_init(&generator, 1000.0f);
    node_vol_init(&volume, 0.5f);

    // Create FIFO for communication
    static struct k_fifo data_fifo;
    k_fifo_init(&data_fifo);

    // Setup producer context
    static struct producer_context prod_ctx = {
        .generator = &generator,
        .output_fifo = &data_fifo,
    };

    // Setup consumer context
    static struct consumer_context cons_ctx = {
        .processor = &volume,
        .input_fifo = &data_fifo,
    };

    // Create threads manually
    K_THREAD_STACK_DEFINE(producer_stack, 1024);
    K_THREAD_STACK_DEFINE(consumer_stack, 1024);

    static struct k_thread producer_thread_data, consumer_thread_data;

    k_thread_create(&producer_thread_data, producer_stack, 1024,
                    producer_thread, &prod_ctx, NULL, NULL,
                    7, 0, K_NO_WAIT);

    k_thread_create(&consumer_thread_data, consumer_stack, 1024,
                    consumer_thread, &cons_ctx, NULL, NULL,
                    7, 0, K_NO_WAIT);

    LOG_INF("Custom threads started - Producer generates, Consumer processes");
}

// ============================================================================
// Example 4: Pipeline Builder (Custom Chain)
// ============================================================================

#define MAX_PIPELINE_NODES 10

struct audio_pipeline {
    struct audio_node *nodes[MAX_PIPELINE_NODES];
    size_t node_count;
};

void pipeline_init(struct audio_pipeline *pipe)
{
    pipe->node_count = 0;
}

void pipeline_add_node(struct audio_pipeline *pipe, struct audio_node *node)
{
    if (pipe->node_count < MAX_PIPELINE_NODES) {
        pipe->nodes[pipe->node_count++] = node;
    }
}

struct audio_block* pipeline_process(struct audio_pipeline *pipe, struct audio_block *in)
{
    struct audio_block *block = in;

    for (size_t i = 0; i < pipe->node_count; i++) {
        block = audio_node_process(pipe->nodes[i], block);
        if (!block) {
            return NULL;  // Node dropped the block
        }
    }

    return block;
}

void example_custom_pipeline(void)
{
    LOG_INF("=== Example: Custom Pipeline Builder ===");

    // Create nodes
    struct audio_node sine, vol1, vol2, analyzer;

    node_sine_init(&sine, 880.0f);
    node_vol_init(&vol1, 0.8f);
    node_vol_init(&vol2, 0.7f);
    node_analyzer_init(&analyzer, 0.9f);

    // Build custom pipeline
    struct audio_pipeline pipeline;
    pipeline_init(&pipeline);
    pipeline_add_node(&pipeline, &sine);
    pipeline_add_node(&pipeline, &vol1);
    pipeline_add_node(&pipeline, &vol2);
    pipeline_add_node(&pipeline, &analyzer);

    // Process blocks
    for (int i = 0; i < 5; i++) {
        struct audio_block *result = pipeline_process(&pipeline, NULL);

        struct analyzer_stats stats;
        node_analyzer_get_stats(&analyzer, &stats);
        LOG_INF("Pipeline block %d: Peak=%.1f dB", i, stats.peak_db);

        audio_block_release(result);
    }
}

// ============================================================================
// Example 5: Batch Processing (Non-Realtime)
// ============================================================================

void example_batch_processing(void)
{
    LOG_INF("=== Example: Batch Processing ===");

    struct audio_node generator, volume;
    node_sine_init(&generator, 440.0f);
    node_vol_init(&volume, 0.5f);

    // Process 1000 blocks as fast as possible (no timing constraints)
    uint32_t start_time = k_uptime_get_32();

    for (int i = 0; i < 1000; i++) {
        struct audio_block *block = audio_node_process(&generator, NULL);
        block = audio_node_process(&volume, block);
        audio_block_release(block);
    }

    uint32_t end_time = k_uptime_get_32();
    LOG_INF("Processed 1000 blocks in %u ms", end_time - start_time);
}

// ============================================================================
// Example 6: Dynamic Node Switching
// ============================================================================

void example_dynamic_switching(void)
{
    LOG_INF("=== Example: Dynamic Node Switching ===");

    struct audio_node sine1, sine2, volume;

    node_sine_init(&sine1, 440.0f);  // A4
    node_sine_init(&sine2, 880.0f);  // A5
    node_vol_init(&volume, 0.7f);

    struct audio_node *active_source = &sine1;

    for (int i = 0; i < 10; i++) {
        // Switch source every 5 blocks
        if (i == 5) {
            active_source = &sine2;
            LOG_INF("Switched to 880 Hz source");
        }

        struct audio_block *block = audio_node_process(active_source, NULL);
        block = audio_node_process(&volume, block);
        audio_block_release(block);

        k_sleep(K_MSEC(50));
    }
}

void main(void)
{
    LOG_INF("=== Standalone Nodes Examples ===\n");

    // Run examples (comment out as needed)
    example_simple_sequential();
    k_sleep(K_SECONDS(1));

    example_custom_threading();
    k_sleep(K_SECONDS(3));

    example_custom_pipeline();
    k_sleep(K_SECONDS(1));

    example_batch_processing();
    k_sleep(K_SECONDS(1));

    example_dynamic_switching();

    LOG_INF("\n=== All examples complete ===");

    while (1) {
        k_sleep(K_SECONDS(10));
    }
}
