#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <audio_fw.h>

static void *setup(void) {
    return NULL;
}

ZTEST_SUITE(audio_cow, NULL, setup, NULL, NULL, NULL);

ZTEST(audio_cow, test_cow_exclusive) {
    /* Test 1: Block with ref_count 1 (exclusive) */
    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Alloc failed");
    
    struct audio_block *original_ptr = block;
    block->data[0] = 42;

    int ret = audio_block_get_writable(&block);
    zassert_equal(ret, 0, "get_writable failed");
    zassert_equal_ptr(block, original_ptr, "Should not copy exclusive block");
    zassert_equal(block->data[0], 42, "Data preserved");
    zassert_equal(atomic_get(&block->ref_count), 1, "Ref count should stay 1");

    audio_block_release(block);
}

ZTEST(audio_cow, test_cow_shared) {
    /* Test 2: Block with ref_count 2 (shared) */
    struct audio_block *block = audio_block_alloc();
    zassert_not_null(block, "Alloc failed");

    block->data[0] = 100;
    atomic_inc(&block->ref_count); /* Simulate sharing (e.g. Splitter) */
    
    struct audio_block *original_ptr = block;
    
    /* Request writable copy */
    int ret = audio_block_get_writable(&block);
    zassert_equal(ret, 0, "get_writable failed");
    
    /* Pointer MUST change */
    zassert_not_equal(block, original_ptr, "Should have created a new block");
    
    /* Data MUST be copied */
    zassert_equal(block->data[0], 100, "Data should be copied");
    
    /* New block MUST be exclusive */
    zassert_equal(atomic_get(&block->ref_count), 1, "New block should be exclusive");
    
    /* Old block MUST have decremented ref count (2 -> 1) */
    zassert_equal(atomic_get(&original_ptr->ref_count), 1, "Original block ref count should dec");

    /* Cleanup */
    audio_block_release(block);       /* Release new block */
    audio_block_release(original_ptr); /* Release original block (simulating the other owner) */
}
