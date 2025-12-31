# Large Window Nodes Design Patterns

## Problem Statement

Standard block size: **128 samples**
FFT/spectral processor needs: **1024 samples** (or more)

**Challenge:** How does a node accumulate multiple blocks before processing?

## Design Patterns

### Pattern 1: Simple Buffering (Block Until Full)

**Use case:** Analysis nodes that don't need continuous output (spectrum analyzer, pitch detector)

```c
/**
 * @brief Context for FFT analyzer node
 */
struct fft_analyzer_ctx {
    int16_t buffer[1024];     // Accumulation buffer
    size_t buffer_pos;        // Current write position
    size_t fft_size;          // 1024

    // FFT working memory
    float complex fft_input[1024];
    float complex fft_output[1024];

    // Results
    float magnitude_spectrum[512];  // Only positive frequencies
    bool spectrum_ready;
};

/**
 * @brief FFT analyzer process function
 *
 * Strategy: Accumulate blocks until buffer is full, then process.
 * Returns input block immediately (pass-through).
 */
static struct audio_block* fft_analyzer_process(struct audio_node *self,
                                                 struct audio_block *in)
{
    if (!in) return NULL;

    struct fft_analyzer_ctx *ctx = (struct fft_analyzer_ctx *)self->ctx;

    // Copy incoming samples to accumulation buffer
    size_t samples_to_copy = in->data_len;
    size_t space_available = ctx->fft_size - ctx->buffer_pos;

    if (samples_to_copy > space_available) {
        samples_to_copy = space_available;
    }

    memcpy(&ctx->buffer[ctx->buffer_pos],
           in->data,
           samples_to_copy * sizeof(int16_t));

    ctx->buffer_pos += samples_to_copy;

    // Check if buffer is full
    if (ctx->buffer_pos >= ctx->fft_size) {
        // Process FFT
        perform_fft(ctx);

        // Reset for next window
        ctx->buffer_pos = 0;
        ctx->spectrum_ready = true;
    }

    // Pass through input unchanged (this is an analyzer, not an effect)
    return in;
}

void perform_fft(struct fft_analyzer_ctx *ctx)
{
    // Convert int16 to float complex
    for (size_t i = 0; i < ctx->fft_size; i++) {
        ctx->fft_input[i] = (float)ctx->buffer[i] / 32768.0f;
    }

    // Perform FFT (using whatever library you have)
    fft_compute(ctx->fft_input, ctx->fft_output, ctx->fft_size);

    // Calculate magnitude spectrum
    for (size_t i = 0; i < ctx->fft_size / 2; i++) {
        float real = crealf(ctx->fft_output[i]);
        float imag = cimagf(ctx->fft_output[i]);
        ctx->magnitude_spectrum[i] = sqrtf(real * real + imag * imag);
    }
}

// Application can read spectrum when ready
int fft_analyzer_get_spectrum(struct audio_node *node,
                               float *spectrum_out,
                               size_t out_size)
{
    struct fft_analyzer_ctx *ctx = (struct fft_analyzer_ctx *)node->ctx;

    if (!ctx->spectrum_ready) {
        return -EAGAIN;  // Not ready yet
    }

    size_t copy_size = (out_size < 512) ? out_size : 512;
    memcpy(spectrum_out, ctx->magnitude_spectrum, copy_size * sizeof(float));

    return 0;
}
```

**Timeline (128-sample blocks @ 48kHz):**
```
Block 0: Accumulate [0..127]     → buffer_pos = 128   (no FFT)
Block 1: Accumulate [128..255]   → buffer_pos = 256   (no FFT)
Block 2: Accumulate [256..383]   → buffer_pos = 384   (no FFT)
Block 3: Accumulate [384..511]   → buffer_pos = 512   (no FFT)
Block 4: Accumulate [512..639]   → buffer_pos = 640   (no FFT)
Block 5: Accumulate [640..767]   → buffer_pos = 768   (no FFT)
Block 6: Accumulate [768..895]   → buffer_pos = 896   (no FFT)
Block 7: Accumulate [896..1023]  → buffer_pos = 1024  → PROCESS FFT! → Reset

Block 8: Accumulate [1024..1151] → buffer_pos = 128   (no FFT)
...repeat...
```

**Characteristics:**
- ✅ Simple implementation
- ✅ Low CPU load (FFT only every 8 blocks)
- ✅ Suitable for visualization/analysis
- ❌ Not suitable for real-time effects (8-block latency)
- ❌ Output not continuous

---

### Pattern 2: Overlap-Add (Spectral Processing)

**Use case:** Real-time effects that process in frequency domain (EQ, reverb, pitch shift)

```c
/**
 * @brief Context for overlap-add FFT processor
 */
struct overlap_add_ctx {
    // Parameters
    size_t fft_size;          // 1024
    size_t hop_size;          // 128 (matches block size!)
    size_t overlap_size;      // 896 (fft_size - hop_size)

    // Input buffering
    int16_t input_buffer[1024];
    size_t input_pos;

    // Output buffering (overlap-add accumulation)
    float output_buffer[1024 + 128];  // Extra space for overlap
    size_t output_read_pos;

    // FFT working memory
    float complex fft_input[1024];
    float complex fft_output[1024];
    float time_domain_output[1024];

    // Window function (Hann window typical for overlap-add)
    float window[1024];

    // Effect-specific processing
    void (*frequency_processor)(float complex *spectrum, size_t size, void *user_data);
    void *processor_data;
};

/**
 * @brief Overlap-add FFT processor
 *
 * Strategy: Continuous processing with overlapping windows
 * Each block triggers one FFT→Process→IFFT cycle
 */
static struct audio_block* overlap_add_process(struct audio_node *self,
                                                struct audio_block *in)
{
    if (!in) return NULL;

    struct overlap_add_ctx *ctx = (struct overlap_add_ctx *)self->ctx;

    // 1. Add new samples to input buffer (shift old samples left)
    memmove(ctx->input_buffer,
            &ctx->input_buffer[ctx->hop_size],
            ctx->overlap_size * sizeof(int16_t));

    memcpy(&ctx->input_buffer[ctx->overlap_size],
           in->data,
           ctx->hop_size * sizeof(int16_t));

    // 2. Apply window and convert to float
    for (size_t i = 0; i < ctx->fft_size; i++) {
        float sample = (float)ctx->input_buffer[i] / 32768.0f;
        ctx->fft_input[i] = sample * ctx->window[i];
    }

    // 3. Forward FFT
    fft_compute(ctx->fft_input, ctx->fft_output, ctx->fft_size);

    // 4. Process in frequency domain (EQ, pitch shift, etc.)
    if (ctx->frequency_processor) {
        ctx->frequency_processor(ctx->fft_output, ctx->fft_size, ctx->processor_data);
    }

    // 5. Inverse FFT
    ifft_compute(ctx->fft_output, ctx->fft_input, ctx->fft_size);

    // 6. Convert back to time domain and apply window again
    for (size_t i = 0; i < ctx->fft_size; i++) {
        ctx->time_domain_output[i] = crealf(ctx->fft_input[i]) * ctx->window[i];
    }

    // 7. Overlap-add: accumulate into output buffer
    for (size_t i = 0; i < ctx->fft_size; i++) {
        ctx->output_buffer[i] += ctx->time_domain_output[i];
    }

    // 8. Extract one hop worth of output
    for (size_t i = 0; i < ctx->hop_size; i++) {
        float sample = ctx->output_buffer[i] * 32768.0f;

        // Clip
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;

        in->data[i] = (int16_t)sample;
    }

    // 9. Shift output buffer left by hop_size
    memmove(ctx->output_buffer,
            &ctx->output_buffer[ctx->hop_size],
            (ctx->fft_size) * sizeof(float));

    // Clear the end
    memset(&ctx->output_buffer[ctx->fft_size], 0, ctx->hop_size * sizeof(float));

    return in;  // Modified in-place
}

/**
 * @brief Initialize overlap-add processor
 */
void node_overlap_add_init(struct audio_node *node,
                            void (*freq_processor)(float complex *, size_t, void *),
                            void *processor_data)
{
    struct overlap_add_ctx *ctx = allocate_context();

    ctx->fft_size = 1024;
    ctx->hop_size = 128;  // Matches block size!
    ctx->overlap_size = ctx->fft_size - ctx->hop_size;

    // Initialize Hann window
    for (size_t i = 0; i < ctx->fft_size; i++) {
        ctx->window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (ctx->fft_size - 1)));
    }

    // Normalize for overlap-add (Hann window with 75% overlap)
    // Normalization factor depends on overlap percentage
    float norm_factor = 0.0f;
    for (size_t i = 0; i < ctx->hop_size; i++) {
        norm_factor += ctx->window[i] * ctx->window[i];
    }
    for (size_t i = 0; i < ctx->fft_size; i++) {
        ctx->window[i] /= sqrtf(norm_factor);
    }

    ctx->frequency_processor = freq_processor;
    ctx->processor_data = processor_data;

    node->ctx = ctx;
    node->vtable = &overlap_add_api;
}
```

**Timeline:**
```
Block 0 (128 samples):
  Input buffer: [0..127] + [old 896 samples]
  → FFT(1024) → Process → IFFT(1024) → Overlap-add → Output[0..127]

Block 1 (128 samples):
  Input buffer: [128..255] + [0..127] + [old 768 samples]
  → FFT(1024) → Process → IFFT(1024) → Overlap-add → Output[128..255]

Block 2 (128 samples):
  Input buffer: [256..383] + [128..255] + [0..127] + [old 640 samples]
  → FFT(1024) → Process → IFFT(1024) → Overlap-add → Output[256..383]

...continuous processing every block...
```

**Characteristics:**
- ✅ Continuous output (every block)
- ✅ Suitable for real-time effects
- ✅ High-quality spectral processing
- ⚠️ High CPU load (FFT every block)
- ⚠️ Latency = fft_size/2 (512 samples = ~10ms @ 48kHz)
- ⚠️ Complex implementation

---

### Pattern 3: Sliding Window (Time-Domain Analysis)

**Use case:** Pitch detection, autocorrelation, adaptive filters

```c
/**
 * @brief Context for sliding window processor
 */
struct sliding_window_ctx {
    int16_t window[1024];
    size_t window_size;
    size_t hop_size;  // How often to process (e.g., every 128 samples)
    size_t samples_since_last_process;

    // Analysis results
    float detected_pitch;
    float confidence;
};

/**
 * @brief Sliding window processor
 *
 * Strategy: Maintain a sliding window, process periodically
 */
static struct audio_block* sliding_window_process(struct audio_node *self,
                                                   struct audio_block *in)
{
    if (!in) return NULL;

    struct sliding_window_ctx *ctx = (struct sliding_window_ctx *)self->ctx;

    // Slide window left by hop_size
    size_t shift_amount = in->data_len;  // Usually 128
    size_t keep_amount = ctx->window_size - shift_amount;

    memmove(ctx->window,
            &ctx->window[shift_amount],
            keep_amount * sizeof(int16_t));

    // Add new samples to end
    memcpy(&ctx->window[keep_amount],
           in->data,
           shift_amount * sizeof(int16_t));

    ctx->samples_since_last_process += shift_amount;

    // Process if hop_size reached
    if (ctx->samples_since_last_process >= ctx->hop_size) {
        // Perform analysis on full window
        autocorrelation_pitch_detect(ctx->window,
                                     ctx->window_size,
                                     &ctx->detected_pitch,
                                     &ctx->confidence);

        ctx->samples_since_last_process = 0;
    }

    // Pass through
    return in;
}
```

**Characteristics:**
- ✅ Simple implementation
- ✅ Flexible hop size (can process every block or less frequently)
- ✅ Low latency (pass-through)
- ✅ Suitable for analysis
- ❌ Not for spectral effects

---

### Pattern 4: Delay Line / Ring Buffer (Variable Delay Effects)

**Use case:** Reverb, chorus, flanger, echo

```c
/**
 * @brief Context for delay-based effect
 */
struct delay_ctx {
    int16_t delay_buffer[48000];  // 1 second @ 48kHz
    size_t buffer_size;
    size_t write_pos;

    // Delay parameters (in samples)
    size_t delay_samples;
    float feedback;
    float mix;
};

/**
 * @brief Delay effect processor
 *
 * Strategy: Ring buffer for arbitrary delay lengths
 */
static struct audio_block* delay_process(struct audio_node *self,
                                         struct audio_block *in)
{
    if (!in) return NULL;

    struct delay_ctx *ctx = (struct delay_ctx *)self->ctx;

    for (size_t i = 0; i < in->data_len; i++) {
        // Calculate read position
        size_t read_pos = (ctx->write_pos + ctx->buffer_size - ctx->delay_samples)
                          % ctx->buffer_size;

        // Read delayed sample
        int16_t delayed = ctx->delay_buffer[read_pos];

        // Mix with input
        float dry = (float)in->data[i];
        float wet = (float)delayed;
        float output = dry * (1.0f - ctx->mix) + wet * ctx->mix;

        // Write to delay buffer (input + feedback)
        float feedback_sample = dry + wet * ctx->feedback;
        if (feedback_sample > 32767.0f) feedback_sample = 32767.0f;
        if (feedback_sample < -32768.0f) feedback_sample = -32768.0f;
        ctx->delay_buffer[ctx->write_pos] = (int16_t)feedback_sample;

        // Advance write position
        ctx->write_pos = (ctx->write_pos + 1) % ctx->buffer_size;

        // Output
        if (output > 32767.0f) output = 32767.0f;
        if (output < -32768.0f) output = -32768.0f;
        in->data[i] = (int16_t)output;
    }

    return in;
}
```

**Characteristics:**
- ✅ Simple concept
- ✅ Any delay length (up to buffer size)
- ✅ Low CPU (sample-by-sample processing)
- ✅ Natural for delay effects
- ⚠️ Memory proportional to max delay time

---

## Comparison Table

| Pattern | Use Case | Latency | CPU | Complexity | Continuous Output |
|---------|----------|---------|-----|------------|-------------------|
| Simple Buffering | Analysis/visualization | High | Low | Simple | No |
| Overlap-Add | Spectral effects | Medium | High | Complex | Yes |
| Sliding Window | Pitch detection | Low | Medium | Simple | Yes (pass-through) |
| Ring Buffer | Delay effects | Configurable | Low | Simple | Yes |

## Choosing a Pattern

### ✅ Use **Simple Buffering** for:
- Spectrum analyzers
- Tempo detection
- One-shot analysis
- When output doesn't need to be continuous

### ✅ Use **Overlap-Add** for:
- Spectral EQ
- Phase vocoder effects (pitch/time stretch)
- Spectral noise reduction
- Any frequency-domain effect

### ✅ Use **Sliding Window** for:
- Autocorrelation pitch detection
- Time-domain feature extraction
- Onset detection
- When you need overlapping analysis windows

### ✅ Use **Ring Buffer** for:
- Delay
- Echo
- Reverb
- Chorus/Flanger
- Any time-based effect

## Memory Considerations

### Static Allocation (Recommended)
```c
#define MAX_FFT_NODES 2

struct fft_analyzer_ctx fft_contexts[MAX_FFT_NODES];
static size_t fft_ctx_index = 0;

void node_fft_analyzer_init(struct audio_node *node)
{
    if (fft_ctx_index >= MAX_FFT_NODES) {
        return;  // Error
    }

    struct fft_analyzer_ctx *ctx = &fft_contexts[fft_ctx_index++];
    // Initialize ctx...

    node->ctx = ctx;
}
```

### Dynamic Allocation (If RAM Abundant)
```c
void node_fft_analyzer_init(struct audio_node *node)
{
    struct fft_analyzer_ctx *ctx = k_malloc(sizeof(struct fft_analyzer_ctx));
    if (!ctx) return;

    // Initialize ctx...
    node->ctx = ctx;
}

void fft_analyzer_cleanup(struct audio_node *node)
{
    k_free(node->ctx);
}
```

## Performance Tips

### 1. **Use Fixed-Point FFT for Embedded**
```c
// Instead of float complex, use Q15 or Q31 fixed-point
// Libraries: CMSIS-DSP, ne10, etc.
arm_rfft_q15(&fft_instance, input_q15, output_q15);
```

### 2. **Optimize Window Functions**
```c
// Pre-compute window at init time
void init_window(float *window, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (size - 1)));
    }
}
```

### 3. **Use Power-of-Two Sizes**
```c
// FFT is MUCH faster with power-of-two sizes
#define FFT_SIZE 1024  // Good
// #define FFT_SIZE 1000  // Bad - use 1024 instead
```

### 4. **Consider Processing Rate**
```c
// Don't need to process EVERY block for visualization
if (ctx->frames_since_last_fft >= PROCESS_INTERVAL) {
    perform_fft(ctx);
    ctx->frames_since_last_fft = 0;
}
```

## Example: Complete Spectrum Analyzer Node

See separate file: `src/nodes/node_spectrum_analyzer.c`

## Summary

**For large window sizes:**

1. **Accumulate samples** in node context buffer
2. **Choose pattern** based on use case:
   - Analysis → Simple buffering
   - Spectral effects → Overlap-add
   - Time-domain analysis → Sliding window
   - Delay effects → Ring buffer
3. **Process when ready** (buffer full or hop size reached)
4. **Pass through** if analysis node, or **modify** if effect node
5. **Manage memory** carefully (large buffers!)

**The sequential processing architecture handles this naturally** - nodes maintain state in their context, and processing happens synchronously within the channel strip thread.
