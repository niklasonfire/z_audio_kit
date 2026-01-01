/**
 * @file main.c
 * @brief Unit tests for Sine Wave Generator Node (V2)
 *
 * Tests verify:
 * - Frequency accuracy
 * - Amplitude correctness
 * - Phase continuity between blocks
 * - DC offset (should be ~0)
 * - Null input handling (generators ignore input)
 * - Reset functionality
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <audio_fw_v2.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Helper function to count zero crossings */
static int count_zero_crossings(struct audio_block *block)
{
    int crossings = 0;
    for (size_t i = 1; i < block->data_len; i++) {
        if ((block->data[i-1] < 0 && block->data[i] >= 0) ||
            (block->data[i-1] >= 0 && block->data[i] < 0)) {
            crossings++;
        }
    }
    return crossings;
}

/* Helper function to calculate RMS amplitude */
static float calculate_rms(struct audio_block *block)
{
    float sum_squares = 0.0f;
    for (size_t i = 0; i < block->data_len; i++) {
        float sample = (float)block->data[i];
        sum_squares += sample * sample;
    }
    return sqrtf(sum_squares / block->data_len);
}

/* Helper function to calculate DC offset (mean) */
static float calculate_dc_offset(struct audio_block *block)
{
    float sum = 0.0f;
    for (size_t i = 0; i < block->data_len; i++) {
        sum += (float)block->data[i];
    }
    return sum / block->data_len;
}

/* Helper function to find peak amplitude */
static int16_t find_peak(struct audio_block *block)
{
    int16_t peak = 0;
    for (size_t i = 0; i < block->data_len; i++) {
        int16_t abs_val = abs(block->data[i]);
        if (abs_val > peak) {
            peak = abs_val;
        }
    }
    return peak;
}

/* Test teardown - clean up any allocated blocks */
static void test_teardown(void *fixture)
{
    ARG_UNUSED(fixture);
    /* Memory cleanup happens via audio_block_release in individual tests */
}

/* Define test suite */
ZTEST_SUITE(node_sine, NULL, NULL, NULL, test_teardown, NULL);

/**
 * @brief Test: Sine generator should produce output with NULL input
 *
 * Generators don't require input blocks - they create their own.
 */
ZTEST(node_sine, test_sine_null_input_handling)
{
    struct audio_node sine;
    node_sine_init(&sine, 1000.0f);

    /* Pass NULL as input - generator should still produce output */
    struct audio_block *block = audio_node_process(&sine, NULL);

    zassert_not_null(block, "Sine generator should produce block with NULL input");
    zassert_not_null(block->data, "Block should have valid data buffer");
    zassert_equal(block->data_len, CONFIG_AUDIO_BLOCK_SAMPLES,
                  "Block should have correct length");

    audio_block_release(block);
}

/**
 * @brief Test: Verify 1kHz sine wave frequency accuracy
 *
 * Count zero crossings to verify frequency is correct.
 * For 1kHz @ 48kHz sample rate with 128 samples:
 * Duration = 128/48000 = 2.667ms
 * Cycles = 1000Hz * 0.002667s = 2.667 cycles
 * Zero crossings = 2.667 * 2 ≈ 5.3 crossings
 */
ZTEST(node_sine, test_sine_frequency_1khz)
{
    struct audio_node sine;
    node_sine_init(&sine, 1000.0f);

    struct audio_block *block = audio_node_process(&sine, NULL);
    zassert_not_null(block, "Failed to generate block");

    int crossings = count_zero_crossings(block);

    /* Allow ±1 crossing tolerance due to phase alignment */
    zassert_true(crossings >= 4 && crossings <= 6,
                 "1kHz should produce ~5 zero crossings, got %d", crossings);

    audio_block_release(block);
}

/**
 * @brief Test: Verify 440Hz sine wave frequency (musical A4)
 *
 * Expected zero crossings for 440Hz @ 48kHz, 128 samples:
 * Duration = 128/48000 = 2.667ms
 * Cycles = 440Hz * 0.002667s = 1.173 cycles
 * Zero crossings = 1.173 * 2 ≈ 2.35 crossings
 */
ZTEST(node_sine, test_sine_frequency_440hz)
{
    struct audio_node sine;
    node_sine_init(&sine, 440.0f);

    struct audio_block *block = audio_node_process(&sine, NULL);
    zassert_not_null(block, "Failed to generate block");

    int crossings = count_zero_crossings(block);

    /* 440Hz should produce 2-3 crossings per block */
    zassert_true(crossings >= 2 && crossings <= 3,
                 "440Hz should produce ~2 zero crossings, got %d", crossings);

    audio_block_release(block);
}

/**
 * @brief Test: Verify amplitude is 50% of full scale
 *
 * Implementation generates at 50% amplitude (INT16_MAX * 0.5f = ~16383)
 */
ZTEST(node_sine, test_sine_amplitude)
{
    struct audio_node sine;
    node_sine_init(&sine, 1000.0f);

    struct audio_block *block = audio_node_process(&sine, NULL);
    zassert_not_null(block, "Failed to generate block");

    int16_t peak = find_peak(block);

    /* Expected peak: INT16_MAX * 0.5 = 16383.5 ≈ 16383 */
    /* Allow ±500 tolerance for rounding/quantization */
    zassert_true(peak >= 15883 && peak <= 16883,
                 "Peak amplitude should be ~16383 (50%%), got %d", peak);

    audio_block_release(block);
}

/**
 * @brief Test: Verify RMS amplitude for sine wave
 *
 * For a sine wave, RMS = Peak / sqrt(2) ≈ Peak * 0.707
 * Expected: 16383 / sqrt(2) ≈ 11585
 */
ZTEST(node_sine, test_sine_rms_amplitude)
{
    struct audio_node sine;
    node_sine_init(&sine, 1000.0f);

    struct audio_block *block = audio_node_process(&sine, NULL);
    zassert_not_null(block, "Failed to generate block");

    float rms = calculate_rms(block);

    /* Expected RMS: ~11585, allow ±1000 tolerance */
    zassert_true(rms >= 10585.0f && rms <= 12585.0f,
                 "RMS should be ~11585, got %.1f", (double)rms);

    audio_block_release(block);
}

/**
 * @brief Test: Verify no DC offset
 *
 * Sine wave should be centered around zero (mean ≈ 0)
 */
ZTEST(node_sine, test_sine_dc_offset)
{
    struct audio_node sine;
    node_sine_init(&sine, 1000.0f);

    struct audio_block *block = audio_node_process(&sine, NULL);
    zassert_not_null(block, "Failed to generate block");

    float dc_offset = calculate_dc_offset(block);

    /* DC offset should be very close to 0, allow ±100 tolerance */
    zassert_true(fabsf(dc_offset) < 100.0f,
                 "DC offset should be ~0, got %.2f", (double)dc_offset);

    audio_block_release(block);
}

/**
 * @brief Test: Verify phase continuity between consecutive blocks
 *
 * The last sample of block N and first sample of block N+1 should
 * maintain phase continuity (no discontinuities/clicks).
 */
ZTEST(node_sine, test_sine_phase_continuity)
{
    struct audio_node sine;
    node_sine_init(&sine, 1000.0f);

    /* Generate first block */
    struct audio_block *block1 = audio_node_process(&sine, NULL);
    zassert_not_null(block1, "Failed to generate first block");
    int16_t last_sample_block1 = block1->data[block1->data_len - 1];

    /* Generate second block */
    struct audio_block *block2 = audio_node_process(&sine, NULL);
    zassert_not_null(block2, "Failed to generate second block");
    int16_t first_sample_block2 = block2->data[0];

    /* Calculate expected phase increment */
    float phase_inc = (2.0f * M_PI * 1000.0f) / CONFIG_AUDIO_SAMPLE_RATE;

    /* Verify the samples follow the expected sine curve */
    /* We can't check exact values due to quantization, but we can verify
     * that the transition is smooth (no huge jumps) */
    int16_t diff = abs(first_sample_block2 - last_sample_block1);

    /* Maximum expected sample-to-sample difference for 1kHz @ 48kHz
     * dV/dt = A*omega*cos(omega*t) max = A*omega = 16383 * (2*pi*1000/48000) ≈ 2144 */
    zassert_true(diff < 3000,
                 "Phase discontinuity detected: diff=%d (expected <3000)", diff);

    audio_block_release(block1);
    audio_block_release(block2);
}

/**
 * @brief Test: Verify reset functionality
 *
 * After reset, phase should return to 0, producing identical output
 */
ZTEST(node_sine, test_sine_reset)
{
    struct audio_node sine;
    node_sine_init(&sine, 1000.0f);

    /* Generate first block */
    struct audio_block *block1 = audio_node_process(&sine, NULL);
    zassert_not_null(block1, "Failed to generate first block");
    int16_t first_sample_1 = block1->data[0];

    /* Process more blocks to advance phase */
    for (int i = 0; i < 5; i++) {
        struct audio_block *temp = audio_node_process(&sine, NULL);
        if (temp) audio_block_release(temp);
    }

    /* Reset node */
    audio_node_reset(&sine);

    /* Generate block after reset */
    struct audio_block *block2 = audio_node_process(&sine, NULL);
    zassert_not_null(block2, "Failed to generate block after reset");
    int16_t first_sample_2 = block2->data[0];

    /* First samples should be identical (phase reset to 0) */
    zassert_equal(first_sample_1, first_sample_2,
                  "Reset should return phase to 0: got %d vs %d",
                  first_sample_1, first_sample_2);

    audio_block_release(block1);
    audio_block_release(block2);
}

/**
 * @brief Test: Verify low frequency generation (100Hz)
 *
 * Test that low frequencies also work correctly
 */
ZTEST(node_sine, test_sine_low_frequency)
{
    struct audio_node sine;
    node_sine_init(&sine, 100.0f);

    struct audio_block *block = audio_node_process(&sine, NULL);
    zassert_not_null(block, "Failed to generate block");

    int crossings = count_zero_crossings(block);

    /* 100Hz @ 48kHz, 128 samples = 0.267 cycles = ~0.5 crossings
     * Due to phase alignment, we might get 0, 1, or 2 crossings */
    zassert_true(crossings >= 0 && crossings <= 2,
                 "100Hz should produce 0-2 zero crossings, got %d", crossings);

    audio_block_release(block);
}

/**
 * @brief Test: Multiple sine nodes can coexist
 *
 * Verify that we can create multiple sine nodes with different frequencies
 */
ZTEST(node_sine, test_multiple_sine_nodes)
{
    struct audio_node sine1, sine2, sine3;

    node_sine_init(&sine1, 440.0f);
    node_sine_init(&sine2, 880.0f);
    node_sine_init(&sine3, 1000.0f);

    /* Generate blocks from all three nodes */
    struct audio_block *block1 = audio_node_process(&sine1, NULL);
    struct audio_block *block2 = audio_node_process(&sine2, NULL);
    struct audio_block *block3 = audio_node_process(&sine3, NULL);

    zassert_not_null(block1, "Failed to generate block from sine1");
    zassert_not_null(block2, "Failed to generate block from sine2");
    zassert_not_null(block3, "Failed to generate block from sine3");

    /* Verify they produce different outputs (different frequencies) */
    int crossings1 = count_zero_crossings(block1);
    int crossings2 = count_zero_crossings(block2);
    int crossings3 = count_zero_crossings(block3);

    /* 880Hz should have more crossings than 440Hz */
    zassert_true(crossings2 >= crossings1,
                 "880Hz should have >= crossings than 440Hz");

    audio_block_release(block1);
    audio_block_release(block2);
    audio_block_release(block3);
}

/**
 * @brief Test: Generator ignores input blocks
 *
 * Even if we pass a valid input block, generator should create new block
 * and release the input
 */
ZTEST(node_sine, test_sine_ignores_input)
{
    struct audio_node sine;
    node_sine_init(&sine, 1000.0f);

    /* Create a dummy input block with known pattern */
    struct audio_block *input = audio_block_alloc();
    zassert_not_null(input, "Failed to allocate input block");

    for (size_t i = 0; i < input->data_len; i++) {
        input->data[i] = 12345; /* Recognizable pattern */
    }

    /* Process with input */
    struct audio_block *output = audio_node_process(&sine, input);
    zassert_not_null(output, "Failed to generate output");

    /* Output should NOT contain the input pattern */
    bool has_input_pattern = false;
    for (size_t i = 0; i < output->data_len; i++) {
        if (output->data[i] == 12345) {
            has_input_pattern = true;
            break;
        }
    }

    zassert_false(has_input_pattern,
                  "Generator should not pass through input data");

    /* Note: We don't release input here because the sine node already did */
    audio_block_release(output);
}
