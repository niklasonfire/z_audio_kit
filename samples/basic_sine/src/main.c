#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "audio_fw.h" // Dein Header

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Stack Sizes */
#define STACK_SIZE 1024
#define PRIORITY 5

/* Nodes */
struct audio_node source;
struct audio_node sink;

/* Thread Stacks */
K_THREAD_STACK_DEFINE(stack_src, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_sink, STACK_SIZE);

int main(void) {
    LOG_INF("Starte z_audio_kit Sample...");

#ifdef CONFIG_AUDIO_FRAMEWORK
    // 1. Init
    // Nutzt das Kconfig aus dem Sample, falls du es implementierst, sonst hardcoded
    node_sine_init(&source, 440.0f); 
    node_log_sink_init(&sink);

    // 2. Wiring
    source.out_fifo = &sink.in_fifo;

    // 3. Start
    audio_node_start(&source, stack_src);
    audio_node_start(&sink, stack_sink);
    
    LOG_INF("Pipeline läuft!");
#else
    LOG_ERR("Audio Framework ist nicht aktiviert! Check prj.conf");
#endif

    return 0;
}