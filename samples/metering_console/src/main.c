#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <audio_fw.h>
#include <stdio.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Nodes */
static struct audio_node source;
static struct audio_node analyzer;
static struct audio_node sink;

/* Stacks */
K_THREAD_STACK_DEFINE(source_stack, 1024);
K_THREAD_STACK_DEFINE(analyzer_stack, 1024);
K_THREAD_STACK_DEFINE(sink_stack, 1024);

void main(void) {
    LOG_INF("Starting Metering Demo...");

    /* 1. Init Nodes */
    node_sine_init(&source, 440.0f);   /* 440Hz Sine Wave */
    node_analyzer_init(&analyzer, 0.3f); /* 30% Smoothing */
    node_log_sink_init(&sink);         /* Just to consume blocks */

    /* 2. Connect Pipeline: Source -> Analyzer -> Sink */
    source.out_fifo = &analyzer.in_fifo;
    analyzer.out_fifo = &sink.in_fifo;

    /* 3. Start Threads */
    audio_node_start(&sink, sink_stack);
    audio_node_start(&analyzer, analyzer_stack);
    audio_node_start(&source, source_stack);

    /* 4. Visualization Loop */
    struct analyzer_stats stats;
    char bar[32];
    
    while(1) {
        k_sleep(K_MSEC(100));

        if (node_analyzer_get_stats(&analyzer, &stats) == 0) {
            /* Draw ASCII Bar */
            int bars = (int)((stats.rms_db + 60.0f) / 2.0f); /* Range -60dB to 0dB -> 0 to 30 chars */
            if (bars < 0) bars = 0;
            if (bars > 30) bars = 30;

            /* Clear buffer */
            for(int i=0; i<30; i++) bar[i] = (i < bars) ? '#' : '.';
            bar[30] = '\0';

            printk("\r[%s] RMS: %6.1f dB | Peak: %6.1f dB %s", 
                   bar, 
                   (double)stats.rms_db, 
                   (double)stats.peak_db,
                   stats.clipping ? "[CLIP]" : "      ");
        }
    }
}
