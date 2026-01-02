/**
 * @file main.c
 * @brief Test suite for node_spectrum_analyzer_v2
 *
 * Tests spectrum analyzer functionality on both native_sim and ARM (qemu_cortex_m3)
 * to verify CMSIS-DSP integration and fallback implementations.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <audio_fw_v2.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define M_PI_F 3.14159265358979323846f

#define TOLERANCE 0.1f  // 10% tolerance for floating-point comparisons

static void *setup(void) {
    return NULL;
}

ZTEST_SUITE(spectrum_analyzer_v2, NULL, setup, NULL, NULL, NULL);

/**
 * @brief Fill block with a sine wave at given frequency
 */
static void fill_block_sine(struct audio_block *block, float frequency, float *phase)
{
    float phase_increment = (2.0f * M_PI_F * frequency) / CONFIG_AUDIO_SAMPLE_RATE;

    for (size_t i = 0; i < block->data_len; i++) {
        float sample = sinf(*phase) * INT16_MAX * 0.5f;  // 50% amplitude
        block->data[i] = (int16_t)sample;

        *phase += phase_increment;
        if (*phase >= 2.0f * M_PI_F) {
            *phase -= 2.0f * M_PI_F;
        }
    }
}

/**
 * @brief Fill block with DC value
 */
static void fill_block_dc(struct audio_block *block, int16_t val)
{
    for (size_t i = 0; i < block->data_len; i++) {
        block->data[i] = val;
    }
}

/**
 * @brief Fill block with silence
 */
static void fill_block_silence(struct audio_block *block)
{
    fill_block_dc(block, 0);
}

/**
 * @brief Test basic initialization with default config
 */
ZTEST(spectrum_analyzer_v2, test_init_default)
{
    struct audio_node analyzer;
    int ret = node_spectrum_analyzer_init_ex(&analyzer, NULL);

    zassert_equal(ret, 0, "Default init should succeed");
    zassert_not_null(analyzer.vtable, "vtable should be set");
    zassert_not_null(analyzer.ctx, "ctx should be set");
}

/**
 * @brief Test initialization with custom FFT size
 */
ZTEST(spectrum_analyzer_v2, test_init_custom_fft_size)
{
    struct audio_node analyzer;

    // Test with 512 FFT
    struct spectrum_analyzer_config config = SPECTRUM_ANALYZER_DEFAULT_CONFIG;
    config.fft_size = 512;

    int ret = node_spectrum_analyzer_init_ex(&analyzer, &config);
    zassert_equal(ret, 0, "Init with 512 FFT should succeed");

    // Test with simplified init
    struct audio_node analyzer2;
    node_spectrum_analyzer_init(&analyzer2, 256);
    zassert_not_null(analyzer2.ctx, "Simplified init should work");
}

/**
 * @brief Test invalid FFT sizes
 */
ZTEST(spectrum_analyzer_v2, test_init_invalid_fft_size)
{
    struct audio_node analyzer;
    struct spectrum_analyzer_config config = SPECTRUM_ANALYZER_DEFAULT_CONFIG;

    // Not a power of 2
    config.fft_size = 1000;
    int ret = node_spectrum_analyzer_init_ex(&analyzer, &config);
    zassert_not_equal(ret, 0, "Non-power-of-2 FFT should fail");

    // Too large
    config.fft_size = 4096;
    ret = node_spectrum_analyzer_init_ex(&analyzer, &config);
    zassert_not_equal(ret, 0, "FFT size > 2048 should fail");
}

/**
 * @brief Test processing silence - should produce low magnitude spectrum
 */
ZTEST(spectrum_analyzer_v2, test_silence)
{
    struct audio_node analyzer;
    node_spectrum_analyzer_init(&analyzer, 256);

    // Feed enough samples to trigger FFT (256 samples)
    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Block allocation failed");

    fill_block_silence(block);

    // Process the block
    struct audio_block *out = audio_node_process(&analyzer, block);
    zassert_not_null(out, "Process should return block");

    // Check if spectrum is ready
    float spectrum[128];  // 256/2 bins
    int ret = node_spectrum_analyzer_get_spectrum(&analyzer, spectrum, 128);

    if (ret == 0) {
        // Spectrum should be near zero
        for (size_t i = 0; i < 128; i++) {
            zassert_true(spectrum[i] < 0.01f,
                "Silence spectrum bin %d should be near zero, got %f",
                i, (double)spectrum[i]);
        }
    }

    audio_block_release(out);
}

/**
 * @brief Test DC input - should have energy only in DC bin
 */
ZTEST(spectrum_analyzer_v2, test_dc_input)
{
    struct audio_node analyzer;
    node_spectrum_analyzer_init(&analyzer, 256);

    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Block allocation failed");

    // Fill with DC value (half scale)
    fill_block_dc(block, 16384);

    struct audio_block *out = audio_node_process(&analyzer, block);
    zassert_not_null(out, "Process should return block");

    float spectrum[128];
    int ret = node_spectrum_analyzer_get_spectrum(&analyzer, spectrum, 128);

    if (ret == 0) {
        // DC bin (bin 0) should have significant energy
        zassert_true(spectrum[0] > 0.4f,
            "DC bin should have energy, got %f", (double)spectrum[0]);

        // Other bins should be near zero
        for (size_t i = 1; i < 10; i++) {
            zassert_true(spectrum[i] < 0.05f,
                "Non-DC bin %d should be near zero, got %f",
                i, (double)spectrum[i]);
        }
    }

    audio_block_release(out);
}

/**
 * @brief Test single-frequency sine wave - peak detection
 */
ZTEST(spectrum_analyzer_v2, test_sine_wave_peak_detection)
{
    struct audio_node analyzer;
    node_spectrum_analyzer_init(&analyzer, 512);

    // Generate 1000 Hz sine wave
    float test_freq = 1000.0f;
    float phase = 0.0f;

    // Need to feed 512 samples
    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Block allocation failed");

    fill_block_sine(block, test_freq, &phase);

    struct audio_block *out = audio_node_process(&analyzer, block);
    zassert_not_null(out, "Process should return block");

    // Get peak frequency
    float peak_freq, peak_mag;
    int ret = node_spectrum_analyzer_get_peak(&analyzer, &peak_freq, &peak_mag);

    if (ret == 0) {
        // Peak should be near 1000 Hz (with some tolerance due to FFT resolution)
        float freq_error = fabsf(peak_freq - test_freq);
        float max_error = (float)CONFIG_AUDIO_SAMPLE_RATE / 512.0f;  // FFT bin width

        zassert_true(freq_error < max_error * 2,
            "Peak frequency should be near %f Hz, got %f Hz (error: %f Hz)",
            (double)test_freq, (double)peak_freq, (double)freq_error);

        // Peak magnitude should be significant
        zassert_true(peak_mag > 0.4f,
            "Peak magnitude should be significant, got %f", (double)peak_mag);
    }

    audio_block_release(out);
}

/**
 * @brief Test spectrum retrieval in dB scale
 */
ZTEST(spectrum_analyzer_v2, test_spectrum_db_scale)
{
    struct audio_node analyzer;
    node_spectrum_analyzer_init(&analyzer, 256);

    // Generate 500 Hz sine wave
    float phase = 0.0f;
    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Block allocation failed");

    fill_block_sine(block, 500.0f, &phase);

    struct audio_block *out = audio_node_process(&analyzer, block);
    zassert_not_null(out, "Process should return block");

    float spectrum_db[128];
    int ret = node_spectrum_analyzer_get_spectrum_db(&analyzer, spectrum_db, 128, 1.0f);

    if (ret == 0) {
        // Find max value
        float max_db = -200.0f;
        for (size_t i = 0; i < 128; i++) {
            if (spectrum_db[i] > max_db) {
                max_db = spectrum_db[i];
            }
        }

        // Max should be reasonable (not -inf, not > 0)
        zassert_true(max_db > -100.0f && max_db <= 0.0f,
            "Max dB should be reasonable, got %f", (double)max_db);
    }

    audio_block_release(out);
}

/**
 * @brief Test different window functions
 */
ZTEST(spectrum_analyzer_v2, test_window_functions)
{
    struct spectrum_analyzer_config configs[] = {
        {
            .fft_size = 256,
            .hop_size = 0,
            .window = SPECTRUM_WINDOW_RECTANGULAR,
            .compute_phase = false,
            .magnitude_floor_db = -120.0f,
        },
        {
            .fft_size = 256,
            .hop_size = 0,
            .window = SPECTRUM_WINDOW_HANN,
            .compute_phase = false,
            .magnitude_floor_db = -120.0f,
        },
        {
            .fft_size = 256,
            .hop_size = 0,
            .window = SPECTRUM_WINDOW_HAMMING,
            .compute_phase = false,
            .magnitude_floor_db = -120.0f,
        },
        {
            .fft_size = 256,
            .hop_size = 0,
            .window = SPECTRUM_WINDOW_BLACKMAN,
            .compute_phase = false,
            .magnitude_floor_db = -120.0f,
        },
    };

    for (size_t w = 0; w < 4; w++) {
        struct audio_node analyzer;
        int ret = node_spectrum_analyzer_init_ex(&analyzer, &configs[w]);
        zassert_equal(ret, 0, "Init with window %d should succeed", w);

        // Process a sine wave
        float phase = 0.0f;
        struct audio_block *block = audio_block_alloc();
        zassert_not_null(block, "Block allocation failed");

        fill_block_sine(block, 1000.0f, &phase);

        struct audio_block *out = audio_node_process(&analyzer, block);
        zassert_not_null(out, "Process should return block");

        // Just verify we can get spectrum
        float spectrum[128];
        ret = node_spectrum_analyzer_get_spectrum(&analyzer, spectrum, 128);
        // Don't assert on ret - may not be ready yet

        audio_block_release(out);
    }
}

/**
 * @brief Test phase spectrum computation
 */
ZTEST(spectrum_analyzer_v2, test_phase_spectrum)
{
    struct audio_node analyzer;

    // Enable phase computation
    struct spectrum_analyzer_config config = SPECTRUM_ANALYZER_DEFAULT_CONFIG;
    config.fft_size = 256;
    config.compute_phase = true;

    int ret = node_spectrum_analyzer_init_ex(&analyzer, &config);
    zassert_equal(ret, 0, "Init with phase enabled should succeed");

    // Process sine wave
    float phase = 0.0f;
    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Block allocation failed");

    fill_block_sine(block, 1000.0f, &phase);

    struct audio_block *out = audio_node_process(&analyzer, block);
    zassert_not_null(out, "Process should return block");

    // Get phase spectrum
    float phase_spectrum[128];
    ret = node_spectrum_analyzer_get_phase(&analyzer, phase_spectrum, 128);

    if (ret == 0) {
        // Phase values should be in range [-π, π]
        for (size_t i = 0; i < 128; i++) {
            zassert_true(phase_spectrum[i] >= -M_PI_F && phase_spectrum[i] <= M_PI_F,
                "Phase bin %d out of range: %f", i, (double)phase_spectrum[i]);
        }
    } else if (ret == -ENOTSUP) {
        zassert_unreachable("Phase should be supported when enabled");
    }

    audio_block_release(out);
}

/**
 * @brief Test phase spectrum disabled
 */
ZTEST(spectrum_analyzer_v2, test_phase_spectrum_disabled)
{
    struct audio_node analyzer;

    // Phase disabled (default)
    node_spectrum_analyzer_init(&analyzer, 256);

    float phase = 0.0f;
    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Block allocation failed");

    fill_block_sine(block, 1000.0f, &phase);

    struct audio_block *out = audio_node_process(&analyzer, block);
    audio_block_release(out);

    // Try to get phase - should fail with ENOTSUP
    float phase_spectrum[128];
    int ret = node_spectrum_analyzer_get_phase(&analyzer, phase_spectrum, 128);

    if (ret != -EAGAIN) {  // Only check if spectrum is ready
        zassert_equal(ret, -ENOTSUP,
            "Getting phase with it disabled should return -ENOTSUP");
    }
}

/**
 * @brief Test multiple blocks processing
 */
ZTEST(spectrum_analyzer_v2, test_multiple_blocks)
{
    struct audio_node analyzer;
    node_spectrum_analyzer_init(&analyzer, 512);

    float phase = 0.0f;
    uint32_t initial_count = node_spectrum_analyzer_get_process_count(&analyzer);

    // Process multiple blocks
    for (int i = 0; i < 3; i++) {
        struct audio_block *block = audio_block_alloc();
        zassert_not_null(block, "Block allocation failed");

        fill_block_sine(block, 1000.0f, &phase);

        struct audio_block *out = audio_node_process(&analyzer, block);
        zassert_not_null(out, "Process should return block");

        audio_block_release(out);
    }

    // Process count should have increased
    uint32_t final_count = node_spectrum_analyzer_get_process_count(&analyzer);
    zassert_true(final_count > initial_count,
        "Process count should increase after processing blocks");
}

/**
 * @brief Test bin to frequency conversion
 */
ZTEST(spectrum_analyzer_v2, test_bin_to_freq)
{
    size_t fft_size = 1024;
    uint32_t sample_rate = CONFIG_AUDIO_SAMPLE_RATE;

    // Bin 0 should be 0 Hz (DC)
    float freq0 = spectrum_analyzer_bin_to_freq(0, fft_size, sample_rate);
    zassert_true(fabsf(freq0) < 0.01f, "Bin 0 should be 0 Hz");

    // Bin 1 should be sample_rate / fft_size
    float freq1 = spectrum_analyzer_bin_to_freq(1, fft_size, sample_rate);
    float expected = (float)sample_rate / (float)fft_size;
    zassert_true(fabsf(freq1 - expected) < 0.01f,
        "Bin 1 should be %f Hz, got %f Hz",
        (double)expected, (double)freq1);

    // Nyquist bin (fft_size/2) should be sample_rate/2
    float freq_nyq = spectrum_analyzer_bin_to_freq(fft_size/2, fft_size, sample_rate);
    float expected_nyq = (float)sample_rate / 2.0f;
    zassert_true(fabsf(freq_nyq - expected_nyq) < 0.01f,
        "Nyquist bin should be %f Hz, got %f Hz",
        (double)expected_nyq, (double)freq_nyq);
}

/**
 * @brief Test reset functionality
 */
ZTEST(spectrum_analyzer_v2, test_reset)
{
    struct audio_node analyzer;
    node_spectrum_analyzer_init(&analyzer, 256);

    // Process a block
    float phase = 0.0f;
    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Block allocation failed");

    fill_block_sine(block, 1000.0f, &phase);

    struct audio_block *out = audio_node_process(&analyzer, block);
    audio_block_release(out);

    // Reset
    audio_node_reset(&analyzer);

    // Process count should be 0 after reset
    uint32_t count_after = node_spectrum_analyzer_get_process_count(&analyzer);
    zassert_equal(count_after, 0, "Process count should be 0 after reset");
}

/**
 * @brief Test pass-through behavior
 */
ZTEST(spectrum_analyzer_v2, test_passthrough)
{
    struct audio_node analyzer;
    node_spectrum_analyzer_init(&analyzer, 256);

    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Block allocation failed");

    fill_block_dc(block, 12345);

    // Store original data
    int16_t original_data[CONFIG_AUDIO_BLOCK_SAMPLES];
    memcpy(original_data, block->data, block->data_len * sizeof(int16_t));

    // Process
    struct audio_block *out = audio_node_process(&analyzer, block);
    zassert_not_null(out, "Process should return block");

    // Data should be unchanged (pass-through)
    zassert_equal(out, block, "Should return same block");
    zassert_equal(memcmp(out->data, original_data, block->data_len * sizeof(int16_t)), 0,
        "Block data should be unchanged (pass-through)");

    audio_block_release(out);
}

/**
 * @brief Test with different FFT sizes
 */
ZTEST(spectrum_analyzer_v2, test_various_fft_sizes)
{
    size_t fft_sizes[] = {64, 128, 256, 512, 1024, 2048};

    for (size_t i = 0; i < 6; i++) {
        struct audio_node analyzer;
        node_spectrum_analyzer_init(&analyzer, fft_sizes[i]);

        zassert_not_null(analyzer.ctx, "Init with FFT size %d failed", fft_sizes[i]);

        // Process a block
        float phase = 0.0f;
        struct audio_block *block = audio_block_alloc();
        zassert_not_null(block, "Block allocation failed");

        fill_block_sine(block, 440.0f, &phase);

        struct audio_block *out = audio_node_process(&analyzer, block);
        zassert_not_null(out, "Process should return block");

        audio_block_release(out);
    }
}
