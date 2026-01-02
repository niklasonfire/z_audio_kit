/**
 * @file node_spectrum_analyzer_v2.c
 * @brief Production-Ready Spectrum Analyzer with CMSIS-DSP Support
 *
 * Features:
 * - CMSIS-DSP FFT on ARM platforms (optimized)
 * - Fallback to basic implementation on other platforms
 * - Configurable FFT size, window type, hop size
 * - Multiple output formats (magnitude, dB, phase)
 */

#include "audio_fw_v2.h"
#include <string.h>
#include <math.h>

// Platform detection
#if defined(__ARM_ARCH) || defined(__arm__) || defined(__ARM_EABI__)
    #define PLATFORM_ARM 1
    #include <arm_math.h>  // CMSIS-DSP
    #include <arm_const_structs.h>
#else
    #define PLATFORM_ARM 0
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define M_PI_F 3.14159265358979323846f

/**
 * @brief Maximum supported FFT size
 */
#define MAX_FFT_SIZE 2048

/**
 * @brief Spectrum analyzer context
 */
struct spectrum_analyzer_ctx {
    // Configuration
    struct spectrum_analyzer_config config;

    // Accumulation buffer
    int16_t sample_buffer[MAX_FFT_SIZE];
    size_t buffer_pos;
    size_t samples_accumulated;

#if PLATFORM_ARM
    // CMSIS-DSP specific
    arm_rfft_fast_instance_f32 fft_instance;
    float32_t fft_input[MAX_FFT_SIZE];
    float32_t fft_output[MAX_FFT_SIZE];
#else
    // Generic fallback
    float fft_input[MAX_FFT_SIZE];
    float fft_output[MAX_FFT_SIZE * 2];  // Complex output (real, imag pairs)
#endif

    // Window function
    float window[MAX_FFT_SIZE];

    // Output spectra
    float magnitude_spectrum[MAX_FFT_SIZE / 2];
    float phase_spectrum[MAX_FFT_SIZE / 2];  // Only if compute_phase enabled
    bool spectrum_ready;

    // Statistics
    uint32_t process_count;
    float peak_frequency;
    float peak_magnitude;
};

/**
 * @brief Generate window function
 */
static void generate_window(float *window,
                            size_t size,
                            enum spectrum_window_type type)
{
    switch (type) {
        case SPECTRUM_WINDOW_RECTANGULAR:
            for (size_t i = 0; i < size; i++) {
                window[i] = 1.0f;
            }
            break;

        case SPECTRUM_WINDOW_HANN:
            for (size_t i = 0; i < size; i++) {
                window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI_F * i / (size - 1)));
            }
            break;

        case SPECTRUM_WINDOW_HAMMING:
            for (size_t i = 0; i < size; i++) {
                window[i] = 0.54f - 0.46f * cosf(2.0f * M_PI_F * i / (size - 1));
            }
            break;

        case SPECTRUM_WINDOW_BLACKMAN:
            for (size_t i = 0; i < size; i++) {
                float a0 = 0.42f;
                float a1 = 0.5f;
                float a2 = 0.08f;
                window[i] = a0 - a1 * cosf(2.0f * M_PI_F * i / (size - 1))
                              + a2 * cosf(4.0f * M_PI_F * i / (size - 1));
            }
            break;

        case SPECTRUM_WINDOW_FLAT_TOP:
            for (size_t i = 0; i < size; i++) {
                float a0 = 1.0f;
                float a1 = 1.93f;
                float a2 = 1.29f;
                float a3 = 0.388f;
                float a4 = 0.028f;
                window[i] = a0 - a1 * cosf(2.0f * M_PI_F * i / (size - 1))
                              + a2 * cosf(4.0f * M_PI_F * i / (size - 1))
                              - a3 * cosf(6.0f * M_PI_F * i / (size - 1))
                              + a4 * cosf(8.0f * M_PI_F * i / (size - 1));
            }
            break;
    }

    // Normalize window (preserve power)
    float power = 0.0f;
    for (size_t i = 0; i < size; i++) {
        power += window[i] * window[i];
    }
    float norm = sqrtf((float)size / power);
    for (size_t i = 0; i < size; i++) {
        window[i] *= norm;
    }
}

#if PLATFORM_ARM
/**
 * @brief Compute FFT using CMSIS-DSP (ARM optimized)
 */
static void compute_fft_cmsis(struct spectrum_analyzer_ctx *ctx)
{
    size_t fft_size = ctx->config.fft_size;

    // Convert int16 to float and apply window
    for (size_t i = 0; i < fft_size; i++) {
        float sample = (float)ctx->sample_buffer[i] / 32768.0f;
        ctx->fft_input[i] = sample * ctx->window[i];
    }

    // Perform real FFT (optimized for real input)
    arm_rfft_fast_f32(&ctx->fft_instance, ctx->fft_input, ctx->fft_output, 0);

    // Compute magnitude (and phase if requested)
    size_t num_bins = fft_size / 2;

    for (size_t i = 0; i < num_bins; i++) {
        float real = ctx->fft_output[i * 2];
        float imag = ctx->fft_output[i * 2 + 1];

        // Magnitude
        arm_sqrt_f32(real * real + imag * imag, &ctx->magnitude_spectrum[i]);

        // Normalize by FFT size
        ctx->magnitude_spectrum[i] /= (float)fft_size;

        // Phase (if requested)
        if (ctx->config.compute_phase) {
            ctx->phase_spectrum[i] = atan2f(imag, real);
        }
    }

    // Find peak
    uint32_t peak_index;
    arm_max_f32(&ctx->magnitude_spectrum[1], num_bins - 1, &ctx->peak_magnitude, &peak_index);
    peak_index += 1;  // Offset for skipping DC bin
    ctx->peak_frequency = (float)peak_index * CONFIG_AUDIO_SAMPLE_RATE / (float)fft_size;
}

#else

/**
 * @brief Naive DFT implementation (fallback for non-ARM platforms)
 *
 * WARNING: This is VERY slow - only for testing on non-ARM platforms!
 * In production on x86/other, use: FFTW, KissFFT, or pffft
 */
static void compute_fft_naive(struct spectrum_analyzer_ctx *ctx)
{
    size_t fft_size = ctx->config.fft_size;
    size_t num_bins = fft_size / 2;

    // Apply window and convert to float
    for (size_t i = 0; i < fft_size; i++) {
        ctx->fft_input[i] = ((float)ctx->sample_buffer[i] / 32768.0f) * ctx->window[i];
    }

    // Naive DFT: X[k] = Σ x[n] * e^(-j*2π*k*n/N)
    for (size_t k = 0; k < num_bins; k++) {
        float real_sum = 0.0f;
        float imag_sum = 0.0f;

        for (size_t n = 0; n < fft_size; n++) {
            float angle = -2.0f * M_PI * k * n / fft_size;
            real_sum += ctx->fft_input[n] * cosf(angle);
            imag_sum += ctx->fft_input[n] * sinf(angle);
        }

        // Magnitude
        ctx->magnitude_spectrum[k] = sqrtf(real_sum * real_sum + imag_sum * imag_sum) / fft_size;

        // Phase (if requested)
        if (ctx->config.compute_phase) {
            ctx->phase_spectrum[k] = atan2f(imag_sum, real_sum);
        }
    }

    // Find peak
    ctx->peak_magnitude = 0.0f;
    size_t peak_index = 0;
    for (size_t i = 1; i < num_bins; i++) {  // Skip DC
        if (ctx->magnitude_spectrum[i] > ctx->peak_magnitude) {
            ctx->peak_magnitude = ctx->magnitude_spectrum[i];
            peak_index = i;
        }
    }
    ctx->peak_frequency = (float)peak_index * CONFIG_AUDIO_SAMPLE_RATE / (float)fft_size;
}

#endif

/**
 * @brief Process function for spectrum analyzer
 */
static struct audio_block* spectrum_analyzer_process(struct audio_node *self,
                                                      struct audio_block *in)
{
    if (!in) {
        return NULL;
    }

    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)self->ctx;
    size_t fft_size = ctx->config.fft_size;
    size_t hop_size = ctx->config.hop_size ? ctx->config.hop_size : fft_size;

    // Accumulate samples
    size_t samples_to_copy = in->data_len;
    size_t space_available = fft_size - ctx->buffer_pos;

    if (samples_to_copy > space_available) {
        samples_to_copy = space_available;
    }

    memcpy(&ctx->sample_buffer[ctx->buffer_pos],
           in->data,
           samples_to_copy * sizeof(int16_t));

    ctx->buffer_pos += samples_to_copy;
    ctx->samples_accumulated += samples_to_copy;

    // Check if we have enough samples to process
    if (ctx->buffer_pos >= fft_size) {
        // Compute FFT (platform-specific)
#if PLATFORM_ARM
        compute_fft_cmsis(ctx);
#else
        compute_fft_naive(ctx);
#endif

        ctx->spectrum_ready = true;
        ctx->process_count++;

        // Handle hop size (overlap)
        if (hop_size < fft_size) {
            // Slide buffer left by hop_size
            memmove(ctx->sample_buffer,
                    &ctx->sample_buffer[hop_size],
                    (fft_size - hop_size) * sizeof(int16_t));
            ctx->buffer_pos = fft_size - hop_size;
        } else {
            // Non-overlapping: reset buffer
            ctx->buffer_pos = 0;
        }
    }

    // Pass through
    return in;
}

/**
 * @brief Reset function
 */
static void spectrum_analyzer_reset(struct audio_node *self)
{
    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)self->ctx;

    ctx->buffer_pos = 0;
    ctx->samples_accumulated = 0;
    ctx->spectrum_ready = false;
    ctx->process_count = 0;
    ctx->peak_frequency = 0.0f;
    ctx->peak_magnitude = 0.0f;

    memset(ctx->sample_buffer, 0, sizeof(ctx->sample_buffer));
    memset(ctx->magnitude_spectrum, 0, sizeof(ctx->magnitude_spectrum));
    memset(ctx->phase_spectrum, 0, sizeof(ctx->phase_spectrum));
}

static const struct audio_node_api spectrum_analyzer_api = {
    .process = spectrum_analyzer_process,
    .reset = spectrum_analyzer_reset,
};

// Static allocation for up to 4 spectrum analyzer nodes
static struct spectrum_analyzer_ctx spectrum_contexts[4];
static size_t spectrum_ctx_index = 0;

/**
 * @brief Initialize spectrum analyzer node with configuration
 *
 * @param node Pointer to node structure
 * @param config Pointer to configuration (NULL for default)
 * @return 0 on success, negative on error
 */
int node_spectrum_analyzer_init_ex(struct audio_node *node,
                                    const struct spectrum_analyzer_config *config)
{
    if (spectrum_ctx_index >= 4) {
        return -ENOMEM;  // Too many nodes
    }

    struct spectrum_analyzer_ctx *ctx = &spectrum_contexts[spectrum_ctx_index++];

    // Use provided config or default
    if (config) {
        ctx->config = *config;
    } else {
        struct spectrum_analyzer_config default_config = SPECTRUM_ANALYZER_DEFAULT_CONFIG;
        ctx->config = default_config;
    }

    // Validate FFT size
    size_t fft_size = ctx->config.fft_size;

    if (fft_size > MAX_FFT_SIZE) {
        return -EINVAL;  // FFT size too large
    }

    if ((fft_size & (fft_size - 1)) != 0) {
        return -EINVAL;  // FFT size not power of 2
    }

    // Initialize CMSIS-DSP on ARM
#if PLATFORM_ARM
    arm_status status;

    // Initialize real FFT instance
    switch (fft_size) {
        case 32:
        case 64:
        case 128:
        case 256:
        case 512:
        case 1024:
        case 2048:
        case 4096:
            status = arm_rfft_fast_init_f32(&ctx->fft_instance, fft_size);
            if (status != ARM_MATH_SUCCESS) {
                return -EINVAL;
            }
            break;
        default:
            return -EINVAL;  // Unsupported FFT size for CMSIS
    }
#endif

    // Generate window function
    generate_window(ctx->window, fft_size, ctx->config.window);

    // Initialize state
    ctx->buffer_pos = 0;
    ctx->samples_accumulated = 0;
    ctx->spectrum_ready = false;
    ctx->process_count = 0;

    memset(ctx->sample_buffer, 0, sizeof(ctx->sample_buffer));
    memset(ctx->magnitude_spectrum, 0, sizeof(ctx->magnitude_spectrum));
    memset(ctx->phase_spectrum, 0, sizeof(ctx->phase_spectrum));

    node->vtable = &spectrum_analyzer_api;
    node->ctx = ctx;

    return 0;
}

/**
 * @brief Initialize spectrum analyzer with default configuration
 */
void node_spectrum_analyzer_init(struct audio_node *node, size_t fft_size)
{
    struct spectrum_analyzer_config config = SPECTRUM_ANALYZER_DEFAULT_CONFIG;
    config.fft_size = fft_size;

    node_spectrum_analyzer_init_ex(node, &config);
}

/**
 * @brief Get magnitude spectrum
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
        return -EAGAIN;
    }

    size_t num_bins = ctx->config.fft_size / 2;
    size_t bins_to_copy = (out_size < num_bins) ? out_size : num_bins;

    memcpy(spectrum_out, ctx->magnitude_spectrum, bins_to_copy * sizeof(float));

    return 0;
}

/**
 * @brief Get spectrum in dB scale
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

    size_t num_bins = ctx->config.fft_size / 2;
    size_t bins_to_copy = (out_size < num_bins) ? out_size : num_bins;

    float floor = powf(10.0f, ctx->config.magnitude_floor_db / 20.0f);

    for (size_t i = 0; i < bins_to_copy; i++) {
        float mag = ctx->magnitude_spectrum[i];

        // Apply floor
        if (mag < floor) {
            mag = floor;
        }

        spectrum_db_out[i] = 20.0f * log10f(mag / reference);
    }

    return 0;
}

/**
 * @brief Get phase spectrum (only if enabled in config)
 */
int node_spectrum_analyzer_get_phase(struct audio_node *node,
                                     float *phase_out,
                                     size_t out_size)
{
    if (!node || !node->ctx || !phase_out) {
        return -EINVAL;
    }

    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)node->ctx;

    if (!ctx->config.compute_phase) {
        return -ENOTSUP;  // Phase computation not enabled
    }

    if (!ctx->spectrum_ready) {
        return -EAGAIN;
    }

    size_t num_bins = ctx->config.fft_size / 2;
    size_t bins_to_copy = (out_size < num_bins) ? out_size : num_bins;

    memcpy(phase_out, ctx->phase_spectrum, bins_to_copy * sizeof(float));

    return 0;
}

/**
 * @brief Get peak frequency and magnitude
 */
int node_spectrum_analyzer_get_peak(struct audio_node *node,
                                    float *peak_freq_out,
                                    float *peak_mag_out)
{
    if (!node || !node->ctx) {
        return -EINVAL;
    }

    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)node->ctx;

    if (!ctx->spectrum_ready) {
        return -EAGAIN;
    }

    if (peak_freq_out) {
        *peak_freq_out = ctx->peak_frequency;
    }

    if (peak_mag_out) {
        *peak_mag_out = ctx->peak_magnitude;
    }

    return 0;
}

/**
 * @brief Convert bin index to frequency
 */
float spectrum_analyzer_bin_to_freq(size_t bin_index,
                                    size_t fft_size,
                                    uint32_t sample_rate)
{
    return (float)bin_index * (float)sample_rate / (float)fft_size;
}

/**
 * @brief Get process count
 */
uint32_t node_spectrum_analyzer_get_process_count(struct audio_node *node)
{
    if (!node || !node->ctx) {
        return 0;
    }

    struct spectrum_analyzer_ctx *ctx = (struct spectrum_analyzer_ctx *)node->ctx;
    return ctx->process_count;
}
