/**
 * @file node_spectrum_analyzer.c
 * @brief Spectrum Analyzer Node - Example of Large Window Processing
 *
 * This node demonstrates how to handle processing that needs more samples
 * than the standard block size (128 samples).
 *
 * FFT size: 1024 samples (8 blocks @ 128 samples/block)
 * Strategy: Simple buffering - accumulate until full, then process
 */

#include "audio_fw_v2.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Spectrum analyzer context
 */
struct spectrum_analyzer_ctx {
    // Accumulation buffer
    int16_t sample_buffer[1024];
    size_t buffer_pos;
    size_t fft_size;

    // FFT working memory (simplified - in real code use CMSIS-DSP or similar)
    float input_float[1024];

    // Output: magnitude spectrum (only positive frequencies)
    float magnitude_spectrum[512];
    bool spectrum_ready;

    // Window function (Hann window for better frequency resolution)
    float window[1024];

    // Processing control
    uint32_t process_count;  // How many FFTs have been computed
};

/**
 * @brief Simple magnitude-only "FFT" for demonstration
 *
 * NOTE: This is a placeholder. In production, use:
 * - CMSIS-DSP: arm_rfft_fast_f32() or arm_rfft_q15()
 * - ne10: ne10_fft_c2c_1d_float32()
 * - KissFFT
 * - Or other optimized library
 */
static void compute_magnitude_spectrum(float *input,
                                       float *magnitude,
                                       size_t fft_size)
{
    // Placeholder: Just compute simple binned energy
    // Real implementation would do proper FFT

    size_t bins = fft_size / 2;
    size_t samples_per_bin = fft_size / bins;

    for (size_t i = 0; i < bins; i++) {
        float energy = 0.0f;
        for (size_t j = 0; j < samples_per_bin; j++) {
            size_t idx = i * samples_per_bin + j;
            if (idx < fft_size) {
                energy += input[idx] * input[idx];
            }
        }
        magnitude[i] = sqrtf(energy / samples_per_bin);
    }
}

/**
 * @brief Process function for spectrum analyzer
 *
 * Accumulates samples until buffer is full, then computes spectrum.
 * Passes input through unchanged (this is an analyzer, not an effect).
 */
static struct audio_block* spectrum_analyzer_process(struct audio_node *self,
                                                      struct audio_block *in)
{
    if (!in) {
        return NULL;
    }

    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)self->ctx;

    // Calculate how many samples we can copy
    size_t samples_to_copy = in->data_len;
    size_t space_available = ctx->fft_size - ctx->buffer_pos;

    if (samples_to_copy > space_available) {
        samples_to_copy = space_available;
    }

    // Copy samples to accumulation buffer
    memcpy(&ctx->sample_buffer[ctx->buffer_pos],
           in->data,
           samples_to_copy * sizeof(int16_t));

    ctx->buffer_pos += samples_to_copy;

    // Check if buffer is full
    if (ctx->buffer_pos >= ctx->fft_size) {
        // Convert to float and apply window
        for (size_t i = 0; i < ctx->fft_size; i++) {
            float sample = (float)ctx->sample_buffer[i] / 32768.0f;
            ctx->input_float[i] = sample * ctx->window[i];
        }

        // Compute spectrum
        compute_magnitude_spectrum(ctx->input_float,
                                   ctx->magnitude_spectrum,
                                   ctx->fft_size);

        // Reset buffer for next window
        ctx->buffer_pos = 0;
        ctx->spectrum_ready = true;
        ctx->process_count++;
    }

    // Pass through input unchanged
    return in;
}

/**
 * @brief Reset function
 */
static void spectrum_analyzer_reset(struct audio_node *self)
{
    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)self->ctx;

    ctx->buffer_pos = 0;
    ctx->spectrum_ready = false;
    ctx->process_count = 0;

    memset(ctx->sample_buffer, 0, sizeof(ctx->sample_buffer));
    memset(ctx->magnitude_spectrum, 0, sizeof(ctx->magnitude_spectrum));
}

static const struct audio_node_api spectrum_analyzer_api = {
    .process = spectrum_analyzer_process,
    .reset = spectrum_analyzer_reset,
};

// Static allocation for up to 4 spectrum analyzer nodes
static struct spectrum_analyzer_ctx spectrum_contexts[4];
static size_t spectrum_ctx_index = 0;

/**
 * @brief Initialize spectrum analyzer node
 *
 * @param node Pointer to node structure
 * @param fft_size FFT size (must be power of 2, typically 512, 1024, 2048)
 */
void node_spectrum_analyzer_init(struct audio_node *node, size_t fft_size)
{
    if (spectrum_ctx_index >= 4) {
        return;  // Error: too many spectrum analyzer nodes
    }

    // Validate FFT size (must be power of 2)
    if ((fft_size & (fft_size - 1)) != 0) {
        return;  // Error: not power of 2
    }

    if (fft_size > 1024) {
        return;  // Error: too large (increase buffer size if needed)
    }

    struct spectrum_analyzer_ctx *ctx = &spectrum_contexts[spectrum_ctx_index++];

    ctx->fft_size = fft_size;
    ctx->buffer_pos = 0;
    ctx->spectrum_ready = false;
    ctx->process_count = 0;

    // Initialize Hann window for better frequency resolution
    // Hann window: w(n) = 0.5 * (1 - cos(2π*n/(N-1)))
    for (size_t i = 0; i < fft_size; i++) {
        ctx->window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (fft_size - 1)));
    }

    memset(ctx->sample_buffer, 0, sizeof(ctx->sample_buffer));
    memset(ctx->magnitude_spectrum, 0, sizeof(ctx->magnitude_spectrum));

    node->vtable = &spectrum_analyzer_api;
    node->ctx = ctx;
}

/**
 * @brief Get the current magnitude spectrum
 *
 * This function can be called from any thread (e.g., UI thread for visualization).
 *
 * @param node Pointer to the spectrum analyzer node
 * @param spectrum_out Output buffer for magnitude spectrum
 * @param out_size Size of output buffer
 * @return 0 on success, -EAGAIN if spectrum not ready yet, -EINVAL on error
 */
int node_spectrum_analyzer_get_spectrum(struct audio_node *node,
                                        float *spectrum_out,
                                        size_t out_size)
{
    if (!node || !node->ctx || !spectrum_out) {
        return -EINVAL;
    }

    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)node->ctx;

    if (!ctx->spectrum_ready) {
        return -EAGAIN;  // Not ready yet (still accumulating samples)
    }

    // Copy available bins (up to output buffer size)
    size_t available_bins = ctx->fft_size / 2;
    size_t bins_to_copy = (out_size < available_bins) ? out_size : available_bins;

    memcpy(spectrum_out, ctx->magnitude_spectrum, bins_to_copy * sizeof(float));

    return 0;
}

/**
 * @brief Get spectrum in dB scale
 *
 * @param node Pointer to the spectrum analyzer node
 * @param spectrum_db_out Output buffer for magnitude spectrum in dB
 * @param out_size Size of output buffer
 * @param reference Reference level for dB calculation (typically 1.0)
 * @return 0 on success, -EAGAIN if spectrum not ready yet, -EINVAL on error
 */
int node_spectrum_analyzer_get_spectrum_db(struct audio_node *node,
                                           float *spectrum_db_out,
                                           size_t out_size,
                                           float reference)
{
    if (!node || !node->ctx || !spectrum_db_out) {
        return -EINVAL;
    }

    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)node->ctx;

    if (!ctx->spectrum_ready) {
        return -EAGAIN;
    }

    size_t available_bins = ctx->fft_size / 2;
    size_t bins_to_copy = (out_size < available_bins) ? out_size : available_bins;

    // Convert to dB: 20 * log10(magnitude / reference)
    for (size_t i = 0; i < bins_to_copy; i++) {
        float mag = ctx->magnitude_spectrum[i];

        // Avoid log(0)
        if (mag < 1e-10f) {
            mag = 1e-10f;
        }

        spectrum_db_out[i] = 20.0f * log10f(mag / reference);
    }

    return 0;
}

/**
 * @brief Get frequency bin value in Hz
 *
 * Helper function to convert bin index to frequency.
 *
 * @param bin_index Index of the frequency bin
 * @param fft_size FFT size used
 * @param sample_rate Sample rate in Hz
 * @return Frequency in Hz
 */
float spectrum_analyzer_bin_to_freq(size_t bin_index,
                                    size_t fft_size,
                                    uint32_t sample_rate)
{
    return (float)bin_index * (float)sample_rate / (float)fft_size;
}

/**
 * @brief Get processing statistics
 *
 * @param node Pointer to the spectrum analyzer node
 * @return Number of FFTs computed so far
 */
uint32_t node_spectrum_analyzer_get_process_count(struct audio_node *node)
{
    if (!node || !node->ctx) {
        return 0;
    }

    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)node->ctx;
    return ctx->process_count;
}
