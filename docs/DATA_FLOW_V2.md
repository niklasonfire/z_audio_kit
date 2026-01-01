# Data Flow in Sequential Architecture (V2)

## Overview

This document describes how audio data flows through the new sequential processing architecture.

## Block-Level Data Flow

### Simple Sequential Processing

```
┌─────────────────────────────────────────────────────────────┐
│  Channel Strip Thread                                       │
│                                                             │
│  1. Block arrives at in_fifo                                │
│     ┌──────────────┐                                        │
│     │ audio_block  │                                        │
│     │ data: [...]  │                                        │
│     │ len: 128     │                                        │
│     └──────┬───────┘                                        │
│            │                                                 │
│  2. Pass to first node                                      │
│            ▼                                                 │
│     ┌─────────────┐                                         │
│     │  Node 1     │ in_block                                │
│     │  (Sine Gen) │────────────▶ Generates samples          │
│     └─────────────┘             Returns new block           │
│            │                                                 │
│            │ Modified block (or new block)                  │
│            ▼                                                 │
│     ┌─────────────┐                                         │
│     │  Node 2     │ in_block                                │
│     │  (Volume)   │────────────▶ Modifies in-place          │
│     └─────────────┘             Returns same block          │
│            │                                                 │
│            │ Same block, volume applied                     │
│            ▼                                                 │
│     ┌─────────────┐                                         │
│     │  Node 3     │ in_block                                │
│     │  (Analyzer) │────────────▶ Analyzes, doesn't modify   │
│     └─────────────┘             Returns same block          │
│            │                                                 │
│            │ Same block, analyzed                           │
│            ▼                                                 │
│  3. Push to out_fifo (or release)                           │
│     ┌──────────────┐                                        │
│     │ audio_block  │                                        │
│     │ (processed)  │                                        │
│     └──────────────┘                                        │
│                                                             │
│  ALL IN ONE THREAD - NO CONTEXT SWITCHES                    │
└─────────────────────────────────────────────────────────────┘

Timeline:
T0: Block received from FIFO
T1: Node 1 processes (50μs)
T2: Node 2 processes (30μs)
T3: Node 3 processes (20μs)
T4: Block sent to output (100μs total)

NO WAITING, NO BLOCKING, DETERMINISTIC
```

## Memory Flow Patterns

### Pattern 1: Transform (In-Place Modification)

Most common pattern for effects nodes.

```
Input:  ┌──────────────┐
        │ audio_block  │
        │ data: [s0,   │
        │        s1,   │
        │        s2,   │
        │        ...]  │
        │ addr: 0x1000 │
        └──────┬───────┘
               │
               ▼
        ┌─────────────┐
        │ Volume Node │
        │ factor: 0.5 │
        └─────────────┘
               │
               │ Modifies samples in-place:
               │ data[0] *= 0.5
               │ data[1] *= 0.5
               │ ...
               ▼
Output: ┌──────────────┐
        │ audio_block  │
        │ data: [s0×0.5│
        │        s1×0.5│
        │        s2×0.5│
        │        ...]  │
        │ addr: 0x1000 │ ◀── SAME ADDRESS
        └──────────────┘

Result: Same block pointer, modified data
Memory allocations: 0
Memory copies: 0
```

### Pattern 2: Generator (Creates New Block)

Used for oscillators, noise generators, input nodes.

```
Input:  ┌──────────────┐
        │ audio_block  │ (ignored, released)
        │ or NULL      │
        └──────────────┘
               │
               ▼
        ┌─────────────┐
        │  Sine Node  │
        │  phase: φ   │
        │  freq: 440  │
        └─────────────┘
               │
               │ 1. Releases input (if present)
               │ 2. Allocates new block
               │ 3. Generates samples
               ▼
Output: ┌──────────────┐
        │ audio_block  │ ◀── NEW BLOCK
        │ data: [sine  │
        │        wave  │
        │        ...]  │
        │ addr: 0x2000 │
        └──────────────┘

Result: New block pointer
Memory allocations: 1
Input block released
```

### Pattern 3: Pass-Through Analyzer

Used for metering, visualization, monitoring.

```
Input:  ┌──────────────┐
        │ audio_block  │
        │ data: [s0,   │
        │        s1,   │
        │        ...]  │
        │ addr: 0x1000 │
        └──────┬───────┘
               │
               ▼
        ┌─────────────┐
        │ Analyzer    │
        │ Node        │
        └─────────────┘
               │
               │ Reads samples (no modification):
               │ peak = max(abs(data[i]))
               │ rms = sqrt(mean(data[i]²))
               │
               │ Stores in ctx->stats
               ▼
Output: ┌──────────────┐
        │ audio_block  │
        │ data: [s0,   │ ◀── UNCHANGED
        │        s1,   │
        │        ...]  │
        │ addr: 0x1000 │ ◀── SAME ADDRESS
        └──────────────┘

Result: Same block pointer, data untouched
Memory allocations: 0
Side effect: Statistics updated in node context
```

## Channel Strip Data Flow

### Single Channel Strip Processing

```
External                Channel Strip
Producer                Thread
   │                         │
   │ 1. Allocate block       │
   ▼                         │
┌────────┐                   │
│ Block  │                   │
└───┬────┘                   │
    │                        │
    │ 2. Fill with data      │
    │    (e.g., from ADC)    │
    ▼                        │
┌────────┐                   │
│ Block  │                   │
│ [data] │                   │
└───┬────┘                   │
    │                        │
    │ 3. Push to strip       │
    │    in_fifo             │
    ▼                        │
  FIFO ──────────────────────┼──▶ 4. Strip pulls block
                             │    (blocks if empty)
                             ▼
                      ┌─────────────┐
                      │ Node 1      │
                      │ process()   │
                      └──────┬──────┘
                             │
                             ▼
                      ┌─────────────┐
                      │ Node 2      │
                      │ process()   │
                      └──────┬──────┘
                             │
                             ▼
                      ┌─────────────┐
                      │ Node 3      │
                      │ process()   │
                      └──────┬──────┘
                             │
                             │ 5. Push to out_fifo
                             ▼
                           FIFO
                             │
                             │ 6. Consumer pulls
                             ▼
                      External Consumer
                      (e.g., DAC, file)

Timing (example):
T0: Block pushed to in_fifo
T1: Strip thread wakes (context switch ~5μs)
T2: Node 1 processes (50μs)
T3: Node 2 processes (30μs)
T4: Node 3 processes (20μs)
T5: Block pushed to out_fifo (~105μs total)

Deterministic latency: ~105μs ±5μs
```

## Mixer Multi-Channel Data Flow

### Lockstep Synchronized Processing

```
External
Producer        Mixer Thread
   │                 │
   │ Block N         │
   ▼                 │
  FIFO ──────────────┼─▶ Block N received
                     │
                     │   Process ALL channels with Block N:
                     │
                     ├─▶ Channel 1 Strip
                     │   ┌──────┐  ┌──────┐  ┌──────┐
                     │   │Node 1│─▶│Node 2│─▶│Node 3│
                     │   └──────┘  └──────┘  └──────┘
                     │        │
                     │        ▼ Result 1
                     │
                     ├─▶ Channel 2 Strip
                     │   ┌──────┐  ┌──────┐  ┌──────┐
                     │   │Node 1│─▶│Node 2│─▶│Node 3│
                     │   └──────┘  └──────┘  └──────┘
                     │        │
                     │        ▼ Result 2
                     │
                     ├─▶ Channel 3 Strip
                     │   ┌──────┐  ┌──────┐  ┌──────┐
                     │   │Node 1│─▶│Node 2│─▶│Node 3│
                     │   └──────┘  └──────┘  └──────┘
                     │        │
                     │        ▼ Result 3
                     │
                     │   Sum all results:
                     │   ┌─────────────────┐
                     │   │ Result 1        │
                     │   │     + Result 2  │
                     │   │     + Result 3  │
                     │   │ = Mixed Block   │
                     │   └────────┬────────┘
                     │            │
                     │            ▼
                     ├─▶ Master Strip
                     │   ┌──────┐  ┌──────┐
                     │   │ Vol  │─▶│Limiter│
                     │   └──────┘  └──────┘
                     │        │
                     │        ▼ Final Output
                     │
                     │   Push to out_fifo
                     ▼
                   FIFO
                     │
                     ▼
              External Consumer

Timeline for Block N:
T0:   Block N arrives
T1:   Ch1 processes (200μs)
T2:   Ch2 processes (200μs)
T3:   Ch3 processes (200μs)
T4:   Sum (10μs)
T5:   Master processes (100μs)
T6:   Output (710μs total)

ONLY THEN does Block N+1 start processing!
ALL CHANNELS GUARANTEED TO PROCESS SAME BLOCK!
```

## Memory Allocation Flow

### Block Lifecycle

```
Memory Slabs                     Active Processing
┌──────────────────┐
│ Data Slab        │
│ ┌──┐┌──┐┌──┐┌──┐│
│ │  ││  ││  ││  ││
│ └──┘└──┘└──┘└──┘│
└──────────────────┘
┌──────────────────┐
│ Metadata Slab    │
│ ┌─┐┌─┐┌─┐┌─┐    │
│ │ ││ ││ ││ │    │
│ └─┘└─┘└─┘└─┘    │
└──────────────────┘
        │
        │ audio_block_alloc()
        ▼
┌──────────────────┐
│ audio_block      │
│ ┌──────────────┐ │
│ │ data ────────┼─┼─▶ [PCM samples]
│ │ data_len: 128│ │
│ └──────────────┘ │
└────────┬─────────┘
         │
         │ Pass through nodes
         ▼
  [Node processing]
         │
         │ Same pointer
         ▼
  [More processing]
         │
         │ Eventually...
         ▼
  audio_block_release()
         │
         ▼
Back to slabs
┌──────────────────┐
│ Data Slab        │
│ ┌──┐┌──┐┌──┐┌──┐│
│ │✓ ││  ││  ││  ││ ◀── Available again
│ └──┘└──┘└──┘└──┘│
└──────────────────┘

Typical journey:
1. Allocated by generator/producer
2. Passed through 5-10 nodes
3. Released by consumer/sink

Total allocations: 1
Total frees: 1
Block pointer changes: 0-1 (only if generator in chain)
Data copies: 0 (all in-place)
```

## Standalone Mode Data Flow

### User-Managed Threading

```
User Code Thread 1          User Code Thread 2
(Producer)                  (Consumer)
     │                            │
     │ Create blocks              │
     ▼                            │
┌─────────┐                       │
│  Node A │ Generate               │
│ (Sine)  │                       │
└────┬────┘                       │
     │                            │
     │ Block                      │
     ▼                            │
  Custom                          │
   FIFO ─────────────────────────┼─▶ Pull block
                                  │
                                  ▼
                            ┌─────────┐
                            │  Node B │ Process
                            │ (Volume)│
                            └────┬────┘
                                  │
                                  ▼
                            ┌─────────┐
                            │  Node C │ Analyze
                            │(Analyzer)│
                            └────┬────┘
                                  │
                                  │ Release
                                  ▼
                            Back to slab

User controls:
- Threading model
- FIFO sizes
- Priorities
- Timing

Framework provides:
- Pure processing functions
- Memory management
```

## ISR-Driven Data Flow (Zero Allocation)

### Lowest Latency Mode

```
Hardware                    ISR Context
   │                             │
   │ DMA Complete                │
   │ Interrupt                   │
   ▼                             │
┌────────────┐                   │
│ DMA Input  │                   │
│ Buffer     │                   │
│ [s0,s1,..] │                   │
└─────┬──────┘                   │
      │                          │
      │ Address passed           │
      ▼                          │
  ┌─────────────────┐            │
  │ Stack Block     │ ◀──────────┼── NO ALLOCATION!
  │ (not allocated) │            │
  │ .data = dma_in  │            │
  │ .len = 128      │            │
  └────────┬────────┘            │
           │                     │
           ▼                     │
    ┌─────────────┐              │
    │ Node 1      │              │
    │ process()   │              │
    └──────┬──────┘              │
           │                     │
           ▼                     │
    ┌─────────────┐              │
    │ Node 2      │              │
    │ process()   │              │
    └──────┬──────┘              │
           │                     │
           ▼                     │
    ┌─────────────┐              │
    │ Node 3      │              │
    │ process()   │              │
    └──────┬──────┘              │
           │                     │
           │ Modified data       │
           ▼                     │
  ┌─────────────────┐            │
  │ memcpy to       │            │
  │ DMA Output      │            │
  │ Buffer          │            │
  └────────┬────────┘            │
           │                     │
           ▼                     │
    Hardware sends               │
    to DAC                       │

Timing (example @ 216MHz Cortex-M7):
  ISR entry:        2μs
  Node 1 (gen):    50μs
  Node 2 (vol):    30μs
  Node 3 (eq):     80μs
  memcpy:          10μs
  ISR exit:         2μs
  Total:         ~174μs

Jitter: ±3μs (only from interrupt latency)
NO scheduler involvement!
NO memory allocation!
NO FIFO operations!
DETERMINISTIC within CPU cycles!
```

## Data Flow Guarantees

### Sequential Processing Guarantees

✅ **Ordering Guarantee**
- Nodes process in array order
- No reordering possible
- Deterministic execution path

✅ **Memory Safety**
- Each block has exactly one owner at a time
- No concurrent access (within a strip)
- No need for locking

✅ **Latency Guarantee**
- Latency = sum(node_processing_times)
- No context switch overhead
- Predictable within μs

✅ **Synchronization Guarantee** (Mixer mode)
- All channels process same block index
- Lockstep processing
- Deterministic relative timing

### What's NOT Guaranteed

❌ **Absolute timing** (unless ISR mode)
- Strip threads can be preempted
- Higher priority threads can delay processing
- Use ISR mode for hard real-time

❌ **Infinite throughput**
- Processing must complete within block period
- 128 samples @ 48kHz = 2.67ms maximum
- Must fit all processing in this window

❌ **Allocation success**
- Memory slabs can be exhausted
- Nodes must handle allocation failures
- Return NULL on failure

## Summary

**Sequential architecture data flow:**

1. **Blocks flow through nodes** - Not between threads
2. **Pointers are passed** - Not data copied
3. **Processing is in-place** - Minimal allocation
4. **Order is guaranteed** - Deterministic path
5. **Timing is predictable** - Sum of node times
6. **Synchronization is natural** - Lockstep processing

**Result: Efficient, deterministic, synchronized audio processing**
