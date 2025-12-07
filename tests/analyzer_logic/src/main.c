#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <audio_fw.h>
#include <math.h>

static void *setup(void) {
    return NULL;
}

ZTEST_SUITE(audio_analyzer, NULL, setup, NULL, NULL, NULL);

static void fill_block_dc(struct audio_block *block, int16_t val) {
    for (size_t i = 0; i < block->data_len; i++) {
        block->data[i] = val;
    }
}

ZTEST(audio_analyzer, test_silence) {
    struct audio_node analyzer;
    node_analyzer_init(&analyzer, 0.0f); /* No smoothing for instant results */

    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Alloc failed");
    fill_block_dc(block, 0);

    /* Push to input FIFO */
    k_fifo_put(&analyzer.in_fifo, block);

    /* Run one cycle manually */
    analyzer.vtable->process(&analyzer);

    /* Check Stats */
    struct analyzer_stats stats;
    int ret = node_analyzer_get_stats(&analyzer, &stats);
    zassert_equal(ret, 0, "get_stats failed");

    zassert_true(stats.rms_db <= -99.0f, "Silence should be ~-100dB, got %f", (double)stats.rms_db);
    zassert_false(stats.clipping, "Should not be clipping");
}

ZTEST(audio_analyzer, test_full_scale) {
    struct audio_node analyzer;
    node_analyzer_init(&analyzer, 0.0f);

    struct audio_block *block = audio_block_alloc();
    fill_block_dc(block, 32767); /* Max positive */

    k_fifo_put(&analyzer.in_fifo, block);
    analyzer.vtable->process(&analyzer);

    struct analyzer_stats stats;
    node_analyzer_get_stats(&analyzer, &stats);

    /* 32767/32768 is approx 0dB */
    zassert_true(stats.rms_db > -0.1f && stats.rms_db <= 0.0f, "Should be ~0dB, got %f", (double)stats.rms_db);
    zassert_true(stats.peak_db > -0.1f, "Peak should be ~0dB");
    zassert_true(stats.clipping, "Should detect clipping at max value");
}

ZTEST(audio_analyzer, test_half_scale) {
    struct audio_node analyzer;
    node_analyzer_init(&analyzer, 0.0f);

    struct audio_block *block = audio_block_alloc();
    fill_block_dc(block, 16384); /* Half scale */

    k_fifo_put(&analyzer.in_fifo, block);
    analyzer.vtable->process(&analyzer);

    struct analyzer_stats stats;
    node_analyzer_get_stats(&analyzer, &stats);

    /* 20*log10(0.5) = -6.02 dB */
    zassert_true(stats.rms_db > -6.1f && stats.rms_db < -5.9f, "Should be ~-6dB, got %f", (double)stats.rms_db);
}

ZTEST(audio_analyzer, test_smoothing) {
    struct audio_node analyzer;
    float smoothing = 0.5f;
    node_analyzer_init(&analyzer, smoothing);

    /* Step 1: Input Silence (Previous State 0.0) */
    /* RMS = 0.0 */
    struct audio_block *block1 = audio_block_alloc();
    fill_block_dc(block1, 0);
    k_fifo_put(&analyzer.in_fifo, block1);
    analyzer.vtable->process(&analyzer);

    /* Step 2: Input Full Scale */
    /* New Raw RMS = 1.0. Smoothed = (0.0 * 0.5) + (1.0 * 0.5) = 0.5 */
    /* 0.5 linear is -6dB */
    struct audio_block *block2 = audio_block_alloc();
    fill_block_dc(block2, 32767);
    k_fifo_put(&analyzer.in_fifo, block2);
    analyzer.vtable->process(&analyzer);

    struct analyzer_stats stats;
    node_analyzer_get_stats(&analyzer, &stats);

    zassert_true(stats.rms_db > -6.1f && stats.rms_db < -5.9f, 
        "With 0.5 smoothing, jump 0->1 should result in 0.5 (-6dB), got %f", (double)stats.rms_db);
}
