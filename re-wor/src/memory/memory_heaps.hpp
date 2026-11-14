#pragma once

#include <cstdint>
#include <cstddef>
#include <rex/ppc.h>
#include <rex/runtime.h>

namespace re_wor::memory {

struct MemoryHeapConfig {
  uint32_t heap_multiplier = 8;
  uint32_t song_heap_mb = 2048;
  uint32_t max_total_heap_mb = 8192;
};

void initialize_memory_heaps(rex::Runtime* runtime, const MemoryHeapConfig& config = {});

void* AllocateExpandedMemory(size_t size, size_t alignment = 16);
void FreeExpandedMemory(void* ptr);

}  // namespace re_wor::memory

namespace re_wor {

using MemoryHeapConfig = re_wor::memory::MemoryHeapConfig;

inline void SetupMemoryHeaps(rex::Runtime* runtime, const MemoryHeapConfig& config = {}) {
  re_wor::memory::initialize_memory_heaps(runtime, config);
}

}  // namespace re_wor
