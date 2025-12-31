/**
 * @file example_spectrum_analyzer.c
 * @brief Example: Using Spectrum Analyzer Node (Large Window Processing)
 *
 * This example demonstrates:
 * 1. How to use a node that needs more samples than block size (1024 vs 128)
 * 2. How buffering works in the sequential architecture
 * 3. How to read results from an analysis node
 */

#include "audio_fw_v2.h"
#include "channel_strip.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spectrum_example, LOG_LEVEL_INF);

// Forward declarations
void node_spectrum_analyzer_init(struct audio_node *node, size_t fft_size);
int node_spectrum_analyzer_get_spectrum(struct audio_node *node,
                                        float *spectrum_out,
                                        size_t out_size);
int node_spectrum_analyzer_get_spectrum_db(struct audio_node *node,
                                           float *spectrum_db_out,
                                           size_t out_size,
                                           float reference);
float spectrum_analyzer_bin_to_freq(size_t bin_index,
                                    size_t fft_size,
                                    uint32_t sample_rate);
uint32_t node_spectrum_analyzer_get_process_count(struct audio_node *node);

// ============================================================================
// Example 1: Simple Spectrum Analysis
// ============================================================================

void example_simple_spectrum_analysis(void)
{
    LOG_INF("=== Example: Simple Spectrum Analysis ===");

    // Create nodes
    struct audio_node generator, analyzer, sink;

    // Generator: 440 Hz sine wave (A4 note)
    node_sine_init(&generator, 440.0f);

    // Analyzer: 1024-point FFT
    node_spectrum_analyzer_init(&analyzer, 1024);

    // Sink: Just to consume the output
    node_log_sink_init(&sink);

    // Create channel strip
    struct channel_strip strip;
    channel_strip_init(&strip, "Analysis");

    channel_strip_add_node(&strip, &generator);
    channel_strip_add_node(&strip, &analyzer);  // Pass-through
    channel_strip_add_node(&strip, &sink);

    // Start processing thread
    K_THREAD_STACK_DEFINE(strip_stack, 2048);
    channel_strip_start(&strip, strip_stack, 2048, 7);

    LOG_INF("Processing started. Waiting for first spectrum...");

    // Wait for analyzer to accumulate enough samples
    // 1024 samples / 128 samples per block = 8 blocks needed
    k_sleep(K_MSEC(100));  // Give it time to accumulate

    // Read spectrum
    float spectrum[512];  // 1024/2 = 512 bins
    int ret = node_spectrum_analyzer_get_spectrum(&analyzer, spectrum, 512);

    if (ret == 0) {
        LOG_INF("Spectrum ready! Showing first 20 bins:");

        for (size_t i = 0; i < 20; i++) {
            float freq = spectrum_analyzer_bin_to_freq(i, 1024, CONFIG_AUDIO_SAMPLE_RATE);
            LOG_INF("  Bin %2zu: %6.1f Hz → Magnitude: %.4f",
                    i, freq, spectrum[i]);
        }

        // Find peak frequency
        size_t peak_bin = 0;
        float peak_mag = 0.0f;
        for (size_t i = 1; i < 512; i++) {  // Skip DC bin
            if (spectrum[i] > peak_mag) {
                peak_mag = spectrum[i];
                peak_bin = i;
            }
        }

        float peak_freq = spectrum_analyzer_bin_to_freq(peak_bin, 1024, CONFIG_AUDIO_SAMPLE_RATE);
        LOG_INF("Peak frequency: %.1f Hz (bin %zu) with magnitude %.4f",
                peak_freq, peak_bin, peak_mag);
        LOG_INF("Expected: 440 Hz");

    } else if (ret == -EAGAIN) {
        LOG_WRN("Spectrum not ready yet (need more samples)");
    }

    channel_strip_stop(&strip);
}

// ============================================================================
// Example 2: Continuous Spectrum Monitoring
// ============================================================================

static struct audio_node monitor_analyzer;
static bool monitoring_active = false;

void spectrum_monitor_thread(void *p1, void *p2, void *p3)
{
    LOG_INF("Spectrum monitor thread started");

    float spectrum_db[512];

    while (monitoring_active) {
        // Try to read spectrum
        int ret = node_spectrum_analyzer_get_spectrum_db(&monitor_analyzer,
                                                         spectrum_db,
                                                         512,
                                                         1.0f);

        if (ret == 0) {
            // Display spectrum as ASCII art (simplified)
            LOG_INF("Spectrum (dB):");

            // Show frequency bands
            const char *bands[] = {
                "  20-100 Hz (Sub)",
                " 100-250 Hz (Bass)",
                " 250-500 Hz (Low Mid)",
                " 500-2kHz (Mid)",
                "  2k-8kHz (High)",
                " 8k-20kHz (Air)"
            };

            // Frequency bin ranges (approximate for 48kHz, 1024 FFT)
            size_t band_bins[][2] = {
                {1, 4},      // 20-100 Hz
                {4, 11},     // 100-250 Hz
                {11, 22},    // 250-500 Hz
                {22, 86},    // 500-2kHz
                {86, 344},   // 2k-8kHz
                {344, 512}   // 8k-20kHz
            };

            for (int band = 0; band < 6; band++) {
                // Average dB in this band
                float avg_db = 0.0f;
                size_t count = 0;

                for (size_t bin = band_bins[band][0]; bin < band_bins[band][1]; bin++) {
                    avg_db += spectrum_db[bin];
                    count++;
                }
                avg_db /= count;

                // Draw bar
                char bar[40];
                int bar_length = (int)((avg_db + 60.0f) / 2.0f);  // -60dB to 0dB → 0 to 30 chars
                if (bar_length < 0) bar_length = 0;
                if (bar_length > 30) bar_length = 30;

                for (int i = 0; i < bar_length; i++) {
                    bar[i] = '=';
                }
                bar[bar_length] = '\0';

                LOG_INF("%s: %6.1f dB %s", bands[band], avg_db, bar);
            }

            LOG_INF("---");
        }

        k_sleep(K_MSEC(500));  // Update twice per second
    }
}

void example_continuous_monitoring(void)
{
    LOG_INF("=== Example: Continuous Spectrum Monitoring ===");

    // Create nodes
    struct audio_node generator, volume;

    // Generator: Sweep from 100Hz to 2kHz
    // (In real code, you'd implement a sweep generator)
    node_sine_init(&generator, 440.0f);

    // Volume control
    node_vol_init(&volume, 0.5f);

    // Analyzer (global)
    node_spectrum_analyzer_init(&monitor_analyzer, 1024);

    // Create strip
    struct channel_strip strip;
    channel_strip_init(&strip, "Monitor");
    channel_strip_add_node(&strip, &generator);
    channel_strip_add_node(&strip, &volume);
    channel_strip_add_node(&strip, &monitor_analyzer);

    // Start processing
    K_THREAD_STACK_DEFINE(strip_stack, 2048);
    channel_strip_start(&strip, strip_stack, 2048, 7);

    // Start monitoring thread
    monitoring_active = true;
    K_THREAD_STACK_DEFINE(monitor_stack, 2048);
    static struct k_thread monitor_thread_data;
    k_thread_create(&monitor_thread_data,
                    monitor_stack,
                    2048,
                    spectrum_monitor_thread,
                    NULL, NULL, NULL,
                    8, 0, K_NO_WAIT);

    LOG_INF("Monitoring for 10 seconds...");
    k_sleep(K_SECONDS(10));

    // Stop monitoring
    monitoring_active = false;
    k_sleep(K_MSEC(100));

    channel_strip_stop(&strip);
    LOG_INF("Monitoring stopped");
}

// ============================================================================
// Example 3: Multiple Frequency Analysis
// ============================================================================

void example_multi_tone_analysis(void)
{
    LOG_INF("=== Example: Multi-Tone Analysis ===");

    // In a real scenario, you'd have multiple sine generators or
    // actual audio input with multiple frequency components

    struct audio_node generator, analyzer;

    // For this example, just one tone
    // (In production, mix multiple generators or use real audio input)
    node_sine_init(&generator, 1000.0f);  // 1 kHz
    node_spectrum_analyzer_init(&analyzer, 1024);

    // Process some blocks manually (non-threaded for simplicity)
    struct audio_block *block;

    for (int i = 0; i < 10; i++) {  // 10 blocks = 1280 samples > 1024 needed
        block = audio_node_process(&generator, NULL);
        block = audio_node_process(&analyzer, block);
        audio_block_release(block);
    }

    // Read spectrum
    float spectrum[512];
    if (node_spectrum_analyzer_get_spectrum(&analyzer, spectrum, 512) == 0) {
        LOG_INF("Finding peaks in spectrum:");

        // Simple peak detection
        for (size_t i = 2; i < 510; i++) {  // Skip edges
            // Peak if higher than neighbors
            if (spectrum[i] > spectrum[i-1] &&
                spectrum[i] > spectrum[i+1] &&
                spectrum[i] > 0.01f) {  // Threshold

                float freq = spectrum_analyzer_bin_to_freq(i, 1024, CONFIG_AUDIO_SAMPLE_RATE);
                LOG_INF("  Peak at %.1f Hz (magnitude: %.4f)", freq, spectrum[i]);
            }
        }
    }
}

// ============================================================================
// Example 4: Understanding Accumulation Timing
// ============================================================================

void example_accumulation_timing(void)
{
    LOG_INF("=== Example: Understanding Accumulation Timing ===");

    struct audio_node generator, analyzer;
    node_sine_init(&generator, 440.0f);
    node_spectrum_analyzer_init(&analyzer, 1024);

    LOG_INF("FFT size: 1024 samples");
    LOG_INF("Block size: %d samples", CONFIG_AUDIO_BLOCK_SAMPLES);
    LOG_INF("Blocks needed: %d", 1024 / CONFIG_AUDIO_BLOCK_SAMPLES);

    uint32_t process_count_before = node_spectrum_analyzer_get_process_count(&analyzer);
    LOG_INF("FFTs computed so far: %u", process_count_before);

    // Process blocks one by one
    for (int block_num = 0; block_num < 10; block_num++) {
        struct audio_block *block = audio_node_process(&generator, NULL);
        block = audio_node_process(&analyzer, block);

        uint32_t count = node_spectrum_analyzer_get_process_count(&analyzer);
        int ret = node_spectrum_analyzer_get_spectrum(&analyzer, NULL, 0);

        LOG_INF("After block %d: FFT count=%u, Ready=%s",
                block_num,
                count,
                (ret == 0) ? "YES" : "NO");

        audio_block_release(block);
    }

    LOG_INF("Timeline:");
    LOG_INF("  Blocks 0-6: Accumulating (buffer filling)");
    LOG_INF("  Block 7: Buffer full → FFT computed → Results available");
    LOG_INF("  Blocks 8-14: Accumulating again");
    LOG_INF("  Block 15: Second FFT computed");
}

// ============================================================================
// Main
// ============================================================================

void main(void)
{
    LOG_INF("=== Spectrum Analyzer Examples ===\n");

    // Run examples
    example_simple_spectrum_analysis();
    k_sleep(K_SECONDS(2));

    example_accumulation_timing();
    k_sleep(K_SECONDS(2));

    example_multi_tone_analysis();
    k_sleep(K_SECONDS(2));

    example_continuous_monitoring();

    LOG_INF("\n=== All examples complete ===");

    while (1) {
        k_sleep(K_SECONDS(10));
    }
}
