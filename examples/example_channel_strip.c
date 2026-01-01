/**
 * @file example_channel_strip.c
 * @brief Example: Using Channel Strip for Deterministic Processing
 *
 * This example shows how to build a channel strip similar to a mixing console:
 * Input → EQ → Compressor → Gate → Volume → Output
 *
 * Benefits:
 * - Deterministic latency (all nodes process sequentially)
 * - Low jitter (no context switching between nodes)
 * - Predictable timing (same execution path every time)
 */

#include "audio_fw_v2.h"
#include "channel_strip.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(example_strip, LOG_LEVEL_INF);

// ============================================================================
// Example: Single Channel Strip
// ============================================================================

void example_single_channel_strip(void)
{
    // 1. Create and initialize nodes
    struct audio_node input, eq, comp, gate, volume;

    node_sine_init(&input, 440.0f);       // Sine generator as input
    // node_eq_init(&eq, ...);            // Placeholder: EQ node
    // node_comp_init(&comp, ...);        // Placeholder: Compressor node
    // node_gate_init(&gate, ...);        // Placeholder: Gate node
    node_vol_init(&volume, 0.5f);         // Volume at 50%

    // 2. Create and configure channel strip
    struct channel_strip strip;
    channel_strip_init(&strip, "Channel 1");

    // 3. Add nodes in processing order
    channel_strip_add_node(&strip, &input);
    // channel_strip_add_node(&strip, &eq);
    // channel_strip_add_node(&strip, &comp);
    // channel_strip_add_node(&strip, &gate);
    channel_strip_add_node(&strip, &volume);

    // 4. Create output sink
    struct audio_node sink;
    node_log_sink_init(&sink);

    // 5. Wire strip output to sink
    // (Sink needs its own thread in this architecture)

    // 6. Start the strip's processing thread
    K_THREAD_STACK_DEFINE(strip_stack, 2048);
    channel_strip_start(&strip, strip_stack, 2048, 7);

    LOG_INF("Channel strip started - processing: Input→Volume");

    // The strip thread now runs continuously:
    // - Waits for blocks on in_fifo
    // - Processes through all nodes sequentially
    // - Pushes to out_fifo
}

// ============================================================================
// Example: Multi-Channel Mixer
// ============================================================================

void example_mixer_console(void)
{
    // Simulate a 4-channel mixing console

    // Create 4 channel strips
    struct channel_strip channels[4];
    struct audio_node inputs[4];
    struct audio_node volumes[4];

    for (int i = 0; i < 4; i++) {
        // Each channel: Input → Volume
        node_sine_init(&inputs[i], 440.0f + (i * 110.0f));  // Different frequencies
        node_vol_init(&volumes[i], 0.25f);  // 25% volume per channel

        channel_strip_init(&channels[i], "Channel");
        channel_strip_add_node(&channels[i], &inputs[i]);
        channel_strip_add_node(&channels[i], &volumes[i]);
    }

    // Create master bus strip
    struct channel_strip master;
    struct audio_node master_vol;
    node_vol_init(&master_vol, 0.8f);  // Master at 80%

    channel_strip_init(&master, "Master");
    channel_strip_add_node(&master, &master_vol);

    // Create mixer
    struct audio_mixer mixer;
    audio_mixer_init(&mixer);

    // Add all channels
    for (int i = 0; i < 4; i++) {
        audio_mixer_add_channel(&mixer, &channels[i]);
    }

    // Set master bus
    audio_mixer_set_master(&mixer, &master);

    // Start mixer thread (processes all channels in lockstep)
    K_THREAD_STACK_DEFINE(mixer_stack, 4096);
    audio_mixer_start(&mixer, mixer_stack, 4096, 6);

    LOG_INF("Mixer started - 4 channels + master bus");

    // The mixer thread now:
    // - Receives input blocks
    // - Processes each channel strip sequentially
    // - Sums all channel outputs
    // - Processes through master bus
    // - Outputs mixed result
}

// ============================================================================
// Example: ISR-Driven Processing (Lowest Latency)
// ============================================================================

static struct channel_strip isr_strip;
static struct audio_node isr_input, isr_volume;

void setup_isr_processing(void)
{
    // Setup channel strip (no threading)
    node_sine_init(&isr_input, 1000.0f);
    node_vol_init(&isr_volume, 0.7f);

    channel_strip_init(&isr_strip, "ISR_Strip");
    channel_strip_add_node(&isr_strip, &isr_input);
    channel_strip_add_node(&isr_strip, &isr_volume);

    // DON'T call channel_strip_start() - we'll call process manually
}

/**
 * @brief Simulated I2S DMA complete interrupt
 *
 * In a real system, this would be triggered by hardware when the
 * DMA transfer completes and new audio data is needed.
 */
void i2s_dma_complete_isr(int16_t *input_buffer, int16_t *output_buffer, size_t frames)
{
    // Create a block wrapper around the DMA buffers (no allocation!)
    struct audio_block in_block = {
        .data = input_buffer,
        .data_len = frames,
    };

    // Process through strip SYNCHRONOUSLY in ISR context
    struct audio_block *out_block = channel_strip_process_block(&isr_strip, &in_block);

    // Copy result to output DMA buffer
    if (out_block) {
        memcpy(output_buffer, out_block->data, frames * sizeof(int16_t));
    }

    // NOTE: In ISR mode, blocks should NOT be allocated/freed
    // Use stack-based blocks or static buffers
}

void main(void)
{
    LOG_INF("=== Audio Framework V2 Examples ===");

    // Choose one example:
    example_single_channel_strip();
    // example_mixer_console();
    // setup_isr_processing();

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
