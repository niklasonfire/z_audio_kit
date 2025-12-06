#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <audio_fw.h> // Dein Header

// Setup: Läuft vor jedem Test
static void *setup(void) {
    return NULL;
}

ZTEST_SUITE(audio_memory, NULL, setup, NULL, NULL, NULL);

/**
 * @brief Testet, ob audio_block_alloc() einen gültigen Block MIT Datenpuffer liefert.
 * Das war dein OOM-Problem: Der Block war da, aber data war NULL.
 */
ZTEST(audio_memory, test_allocation_integrity) {
    struct audio_block *block = audio_block_alloc();

    // 1. Ist der Block an sich da?
    zassert_not_null(block, "Fehler: audio_block_alloc gab NULL zurück (Pool leer?)");

    // 2. Hat der Block Speicher für Samples? (Hier lag der Fehler!)
    zassert_not_null(block->data, "Fehler: block->data ist NULL! Slab-Konfiguration prüfen!");
    
    // 3. Stimmt die Länge?
    zassert_equal(block->data_len, 128, "Fehler: Blocklänge ist %d, erwartet 128", block->data_len);

    // Aufräumen
    audio_block_release(block);
}

/**
 * @brief Testet Stress: Können wir mehrere Blöcke holen?
 */
ZTEST(audio_memory, test_pool_capacity) {
    struct audio_block *blocks[4];
    int i;

    for(i=0; i<4; i++) {
        blocks[i] = audio_block_alloc();
        if (!blocks[i]) break;
    }

    // Wir erwarten, dass wir wenigstens 4 Blöcke für eine Pipeline bekommen
    zassert_equal(i, 4, "Konnte nur %d von 4 Blöcken allocieren. Pool zu klein!", i);

    // Alles zurückgeben
    for(int j=0; j<i; j++) {
        audio_block_release(blocks[j]);
    }
}