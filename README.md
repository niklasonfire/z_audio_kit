# Zephyr Audio Pipeline Framework

## 📖 Overview

This project implements a modular, high-performance audio processing framework for the **Zephyr RTOS**. It is designed to be hardware-agnostic, efficient, and strictly adherent to **SOLID software design principles**.

Unlike traditional embedded audio solutions that rely heavily on hardware-specific DMA couplings or raw byte streams (Ringbuffers), this framework uses a **Block-Passing Architecture** with **Zero-Copy** semantics. This ensures deterministic behavior, low CPU overhead, and high portability across different microcontroller architectures (ESP32, STM32, NRF52, etc.).

---

## 🏗 Architecture & Design Decisions

### 1. The "Block-Passing" Concept
The framework moves away from byte-streaming. Instead, it treats audio data as discrete "Chunks" or "Blocks" (default: 128 samples). This approach is inspired by professional audio tools (like Teensy Audio Library) but adapted for the Zephyr ecosystem.

* **Memory Management (`k_mem_slab`):** Audio blocks are pre-allocated in a fixed-size memory pool. This guarantees O(1) allocation/deallocation time and prevents heap fragmentation.
* **Transport (`k_fifo`):** Nodes communicate by passing *pointers* to these blocks via FIFO queues. No data is copied between processing nodes (**Zero-Copy**).
* **Life-Cycle Management (Reference Counting):** A `ref_count` mechanism allows a single audio block to be processed by multiple consumers simultaneously (e.g., Speaker + SD Card) without race conditions or memory leaks.

### 2. SOLID Compliance
The architecture is built to be robust and maintainable:
* **Single Responsibility:** Nodes focus solely on their processing logic. Memory management is handled by the core infrastructure.
* **Open/Closed:** New audio effects (Nodes) can be added without modifying existing code.
* **Dependency Inversion:** Nodes are not hard-wired. The pipeline topology is defined at runtime via "Dependency Injection" (connecting FIFOs in `main.c`).

### 3. Node Topology
The system is built as a directed graph of "Nodes". A Node is an abstract entity that performs a specific task on the audio stream.

| Node Type | Function | I/O Pattern | Example |
| :--- | :--- | :--- | :--- |
| **Producer (Source)** | Generates data | `(void) -> Block` | I2S Mic, Sine Gen, File Reader |
| **Transform** | Modifies data | `Block -> Block` | Volume, Equalizer, Resampler |
| **Consumer (Sink)** | Consumes data | `Block -> (void)` | I2S DAC, Log Output, WAV Writer |
| **Router** | Directs flow | `1-in -> N-out` | Splitter, Mixer |

### 4. Zero-Copy & Copy-on-Write (CoW)
To enable efficient "Fan-Out" (splitting one stream to multiple destinations), the framework uses reference counting.

*   **Sharing:** The `Splitter` node increments the `ref_count` of a block and sends the *same pointer* to all outputs.
*   **Safety (CoW):** Nodes that **modify** data (like `Volume`) must ensure they have exclusive access.
    *   The helper `audio_block_get_writable(&block)` checks the `ref_count`.
    *   If `ref_count > 1` (Shared), it allocates a **new block**, copies the data, releases the old block, and updates the pointer.
    *   If `ref_count == 1` (Exclusive), it returns immediately (Zero-Copy).

**⚠️ Warning: Copy Storm**
If a Splitter feeds multiple modifying nodes, each node will trigger a copy. Ensure `CONFIG_AUDIO_MEM_SLAB_COUNT` is sufficient to handle this instantaneous spike in allocation.

---

## 📦 Directory Structure

The project is structured as a standard Zephyr module for easy integration.

```text
audio_framework/
├── CMakeLists.txt              # Zephyr Build System integration
├── Kconfig                     # Menuconfig options (Block size, Stack size)
├── zephyr_module.yml           # Module definition
├── include/
│   └── audio_fw.h              # Public API and Interfaces
└── src/
    ├── core.c                  # Memory Slabs & Ref-Counting implementation
    └── nodes/
        ├── node_sine.c         # [Producer] Sine Wave Generator
        ├── node_volume.c       # [Transform] Volume Control
        ├── node_splitter.c     # [Router] 1-in-N-out Splitter (Ref-Counting Demo)
        └── node_log_sink.c     # [Consumer] Debug Logging Sink
