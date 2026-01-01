/**
 * @file example_spectrum_analyzer_advanced.c
 * @brief Advanced Spectrum Analyzer Examples with CMSIS-DSP and Configuration
 *
 * Demonstrates:
 * - Configurable spectrum analyzer
 * - Different window functions
 * - Overlapping vs non-overlapping analysis
 * - Phase spectrum
 * - Platform-specific optimizations (ARM CMSIS-DSP)
 */

#include "audio_fw_v2.h"
#include "channel_strip.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spectrum_advanced, LOG_LEVEL_INF);

// ============================================================================
// Example 1: Using Configuration Struct
// ============================================================================

void example_configured_analyzer(void)
{
    LOG_INF("=== Example: Configured Spectrum Analyzer ===");

    // Create custom configuration
    struct spectrum_analyzer_config config = {
        .fft_size = 2048,                      // Larger FFT = better frequency resolution
        .hop_size = 512,                       // 75% overlap (2048 - 512 = 1536 overlap)
        .window = SPECTRUM_WINDOW_BLACKMAN,    // Better sidelobe suppression
        .compute_phase = true,                 // Enable phase computation
        .magnitude_floor_db = -100.0f,         // Floor at -100 dB
    };

    struct audio_node generator, analyzer;

    // 1 kHz sine wave
    node_sine_init(&generator, 1000.0f);

    // Initialize with custom config
    int ret = node_spectrum_analyzer_init_ex(&analyzer, &config);
    if (ret != 0) {
        LOG_ERR("Failed to initialize analyzer: %d", ret);
        return;
    }

#if defined(__ARM_ARCH) || defined(__arm__)
    LOG_INF("Using CMSIS-DSP optimized FFT (ARM platform)");
#else
    LOG_INF("Using fallback FFT (non-ARM platform)");
    LOG_WRN("WARNING: Non-ARM fallback is slow! Use for testing only.");
#endif

    LOG_INF("Configuration:");
    LOG_INF("  FFT size: %zu", config.fft_size);
    LOG_INF("  Hop size: %zu (%zu%% overlap)",
            config.hop_size,
            (config.fft_size - config.hop_size) * 100 / config.fft_size);
    LOG_INF("  Window: Blackman");
    LOG_INF("  Phase computation: %s", config.compute_phase ? "Enabled" : "Disabled");

    // Process enough blocks to fill buffer
    // 2048 samples / 128 samples per block = 16 blocks
    for (int i = 0; i < 20; i++) {
        struct audio_block *block = audio_node_process(&generator, NULL);
        block = audio_node_process(&analyzer, block);
        audio_block_release(block);
    }

    // Read spectrum
    float spectrum[1024];  // 2048/2 bins
    float phase[1024];

    if (node_spectrum_analyzer_get_spectrum(&analyzer, spectrum, 1024) == 0) {
        LOG_INF("Spectrum computed successfully");

        // Find peak
        float peak_freq, peak_mag;
        node_spectrum_analyzer_get_peak(&analyzer, &peak_freq, &peak_mag);
        LOG_INF("Peak at %.2f Hz (magnitude: %.6f)", peak_freq, peak_mag);
        LOG_INF("Expected: 1000.00 Hz");

        // Read phase at peak
        if (node_spectrum_analyzer_get_phase(&analyzer, phase, 1024) == 0) {
            size_t peak_bin = (size_t)(peak_freq * config.fft_size / CONFIG_AUDIO_SAMPLE_RATE);
            LOG_INF("Phase at peak: %.3f radians", phase[peak_bin]);
        }
    }
}

// ============================================================================
// Example 2: Comparing Window Functions
// ============================================================================

void example_window_comparison(void)
{
    LOG_INF("\n=== Example: Window Function Comparison ===");

    enum spectrum_window_type windows[] = {
        SPECTRUM_WINDOW_RECTANGULAR,
        SPECTRUM_WINDOW_HANN,
        SPECTRUM_WINDOW_HAMMING,
        SPECTRUM_WINDOW_BLACKMAN,
        SPECTRUM_WINDOW_FLAT_TOP,
    };

    const char *window_names[] = {
        "Rectangular",
        "Hann",
        "Hamming",
        "Blackman",
        "Flat-Top",
    };

    // Test signal: 1000 Hz sine wave
    struct audio_node generator;
    node_sine_init(&generator, 1000.0f);

    LOG_INF("Test signal: 1000 Hz sine wave");
    LOG_INF("Comparing window functions:\n");

    for (size_t w = 0; w < 5; w++) {
        struct spectrum_analyzer_config config = {
            .fft_size = 1024,
            .hop_size = 0,
            .window = windows[w],
            .compute_phase = false,
            .magnitude_floor_db = -120.0f,
        };

        struct audio_node analyzer;
        if (node_spectrum_analyzer_init_ex(&analyzer, &config) != 0) {
            continue;
        }

        // Reset generator
        audio_node_reset(&generator);

        // Process blocks
        for (int i = 0; i < 10; i++) {
            struct audio_block *block = audio_node_process(&generator, NULL);
            block = audio_node_process(&analyzer, block);
            audio_block_release(block);
        }

        // Get peak
        float peak_freq, peak_mag;
        if (node_spectrum_analyzer_get_peak(&analyzer, &peak_freq, &peak_mag) == 0) {
            float error_hz = peak_freq - 1000.0f;
            float error_cents = 1200.0f * log2f(peak_freq / 1000.0f);

            LOG_INF("%12s window: Peak at %7.2f Hz (error: %+6.2f Hz, %+5.1f cents)",
                    window_names[w],
                    peak_freq,
                    error_hz,
                    error_cents);
            LOG_INF("              Magnitude: %.6f\n", peak_mag);
        }
    }

    LOG_INF("Notes:");
    LOG_INF("  - Rectangular: Narrowest main lobe, worst sidelobes");
    LOG_INF("  - Hann/Hamming: Good compromise");
    LOG_INF("  - Blackman: Better sidelobe suppression");
    LOG_INF("  - Flat-Top: Best amplitude accuracy");
}

// ============================================================================
// Example 3: Overlap Analysis
// ============================================================================

void example_overlap_analysis(void)
{
    LOG_INF("\n=== Example: Overlap Analysis ===");

    struct audio_node generator;
    node_sine_init(&generator, 440.0f);

    // Test different overlap amounts
    size_t hop_sizes[] = {1024, 512, 256, 128};  // 0%, 50%, 75%, 87.5% overlap
    const char *overlap_names[] = {"No overlap", "50% overlap", "75% overlap", "87.5% overlap"};

    for (size_t h = 0; h < 4; h++) {
        struct spectrum_analyzer_config config = {
            .fft_size = 1024,
            .hop_size = hop_sizes[h],
            .window = SPECTRUM_WINDOW_HANN,
            .compute_phase = false,
            .magnitude_floor_db = -120.0f,
        };

        struct audio_node analyzer;
        if (node_spectrum_analyzer_init_ex(&analyzer, &config) != 0) {
            continue;
        }

        // Reset generator
        audio_node_reset(&generator);

        // Process 20 blocks and count how many FFTs are computed
        uint32_t ffts_before = node_spectrum_analyzer_get_process_count(&analyzer);

        for (int i = 0; i < 20; i++) {
            struct audio_block *block = audio_node_process(&generator, NULL);
            block = audio_node_process(&analyzer, block);
            audio_block_release(block);
        }

        uint32_t ffts_after = node_spectrum_analyzer_get_process_count(&analyzer);
        uint32_t ffts_computed = ffts_after - ffts_before;

        float update_rate = (float)ffts_computed / 20.0f;

        LOG_INF("%s (hop=%zu):", overlap_names[h], hop_sizes[h]);
        LOG_INF("  FFTs in 20 blocks: %u", ffts_computed);
        LOG_INF("  Update rate: %.2f FFTs per block", update_rate);
        LOG_INF("  CPU load: %s\n",
                update_rate > 0.5f ? "HIGH" : update_rate > 0.2f ? "MEDIUM" : "LOW");
    }

    LOG_INF("Overlap trade-offs:");
    LOG_INF("  More overlap = More updates = Better time resolution = Higher CPU");
    LOG_INF("  Less overlap = Fewer updates = Lower CPU = Less smooth tracking");
}

// ============================================================================
// Example 4: Real-Time Pitch Detection
// ============================================================================

void example_pitch_detection(void)
{
    LOG_INF("\n=== Example: Real-Time Pitch Detection ===");

    // Musical notes (A4 and harmonics)
    float test_frequencies[] = {
        440.0f,   // A4
        880.0f,   // A5
        1760.0f,  // A6
        220.0f,   // A3
    };

    const char *note_names[] = {"A4", "A5", "A6", "A3"};

    struct spectrum_analyzer_config config = {
        .fft_size = 2048,         // High resolution for accurate pitch
        .hop_size = 256,          // Fast updates
        .window = SPECTRUM_WINDOW_BLACKMAN,  // Good sidelobe suppression
        .compute_phase = false,
        .magnitude_floor_db = -100.0f,
    };

    LOG_INF("Pitch detection configuration:");
    LOG_INF("  FFT size: %zu (resolution: %.2f Hz)",
            config.fft_size,
            (float)CONFIG_AUDIO_SAMPLE_RATE / config.fft_size);

    for (size_t f = 0; f < 4; f++) {
        struct audio_node generator, analyzer;

        node_sine_init(&generator, test_frequencies[f]);
        if (node_spectrum_analyzer_init_ex(&analyzer, &config) != 0) {
            continue;
        }

        // Process blocks
        for (int i = 0; i < 20; i++) {
            struct audio_block *block = audio_node_process(&generator, NULL);
            block = audio_node_process(&analyzer, block);
            audio_block_release(block);
        }

        // Detect pitch
        float detected_freq, magnitude;
        if (node_spectrum_analyzer_get_peak(&analyzer, &detected_freq, &magnitude) == 0) {
            float error_hz = detected_freq - test_frequencies[f];
            float error_cents = 1200.0f * log2f(detected_freq / test_frequencies[f]);

            LOG_INF("%s (%.2f Hz): Detected %.2f Hz (error: %+.2f Hz, %+.1f cents)",
                    note_names[f],
                    test_frequencies[f],
                    detected_freq,
                    error_hz,
                    error_cents);
        }
    }

    LOG_INF("\nNote: For production pitch detection, use autocorrelation or");
    LOG_INF("      more sophisticated methods (YIN, SWIPE, etc.)");
}

// ============================================================================
// Example 5: Platform-Specific Optimizations
// ============================================================================

void example_platform_info(void)
{
    LOG_INF("\n=== Example: Platform Information ===");

#if defined(__ARM_ARCH) || defined(__arm__)
    LOG_INF("Platform: ARM");
    LOG_INF("CMSIS-DSP: Available");

#if defined(__ARM_FEATURE_DSP)
    LOG_INF("DSP Extensions: Yes");
#else
    LOG_INF("DSP Extensions: No");
#endif

#if defined(__ARM_ARCH_7EM__)
    LOG_INF("Architecture: ARMv7E-M (Cortex-M4/M7 class)");
#elif defined(__ARM_ARCH_8M_MAIN__)
    LOG_INF("Architecture: ARMv8-M Mainline (Cortex-M33/M55 class)");
#elif defined(__ARM_ARCH_6M__)
    LOG_INF("Architecture: ARMv6-M (Cortex-M0/M0+ class)");
#endif

#if defined(ARM_MATH_CM4) || defined(ARM_MATH_CM7)
    LOG_INF("CMSIS-DSP Optimizations: Cortex-M4/M7 SIMD");
#endif

    LOG_INF("\nSupported CMSIS FFT sizes:");
    LOG_INF("  32, 64, 128, 256, 512, 1024, 2048, 4096");

    LOG_INF("\nPerformance tips:");
    LOG_INF("  - Use power-of-2 FFT sizes");
    LOG_INF("  - Enable FPU in compiler (-mfpu=fpv4-sp-d16 or similar)");
    LOG_INF("  - Use Q15 fixed-point FFT for even faster processing");

#else
    LOG_INF("Platform: Non-ARM (x86, RISC-V, etc.)");
    LOG_INF("CMSIS-DSP: NOT available");
    LOG_INF("FFT Implementation: Naive DFT (VERY SLOW!)");
    LOG_INF("\nWARNING: This is a fallback for testing only!");
    LOG_INF("For production on non-ARM platforms, integrate:");
    LOG_INF("  - FFTW (GPL or commercial license)");
    LOG_INF("  - KissFFT (BSD license)");
    LOG_INF("  - pffft (BSD-like license)");
#endif
}

// ============================================================================
// Example 6: Default vs Custom Configuration
// ============================================================================

void example_config_comparison(void)
{
    LOG_INF("\n=== Example: Default vs Custom Configuration ===");

    struct audio_node generator, analyzer_default, analyzer_custom;

    node_sine_init(&generator, 440.0f);

    // Default configuration (simple init)
    LOG_INF("Default configuration:");
    node_spectrum_analyzer_init(&analyzer_default, 1024);
    LOG_INF("  FFT size: 1024");
    LOG_INF("  Window: Hann");
    LOG_INF("  Overlap: None");
    LOG_INF("  Phase: Disabled\n");

    // Custom configuration
    LOG_INF("Custom configuration:");
    struct spectrum_analyzer_config custom_config = {
        .fft_size = 2048,
        .hop_size = 512,
        .window = SPECTRUM_WINDOW_BLACKMAN,
        .compute_phase = true,
        .magnitude_floor_db = -100.0f,
    };
    node_spectrum_analyzer_init_ex(&analyzer_custom, &custom_config);
    LOG_INF("  FFT size: 2048");
    LOG_INF("  Window: Blackman");
    LOG_INF("  Overlap: 75%%");
    LOG_INF("  Phase: Enabled\n");

    LOG_INF("Use default for: Quick analysis, low CPU, simple applications");
    LOG_INF("Use custom for: Precise measurements, research, advanced features");
}

// ============================================================================
// Main
// ============================================================================

void main(void)
{
    LOG_INF("╔════════════════════════════════════════════════════════════╗");
    LOG_INF("║   Advanced Spectrum Analyzer Examples                     ║");
    LOG_INF("║   Featuring CMSIS-DSP and Configurable Processing         ║");
    LOG_INF("╚════════════════════════════════════════════════════════════╝\n");

    // Platform info first
    example_platform_info();
    k_sleep(K_SECONDS(2));

    // Configuration examples
    example_config_comparison();
    k_sleep(K_SECONDS(2));

    example_configured_analyzer();
    k_sleep(K_SECONDS(2));

    example_window_comparison();
    k_sleep(K_SECONDS(2));

    example_overlap_analysis();
    k_sleep(K_SECONDS(2));

    example_pitch_detection();

    LOG_INF("\n╔════════════════════════════════════════════════════════════╗");
    LOG_INF("║   All examples complete!                                   ║");
    LOG_INF("╚════════════════════════════════════════════════════════════╝");

    while (1) {
        k_sleep(K_SECONDS(10));
    }
}
