#include "memory/memory_heaps.hpp"
#include "core/crash_handler.hpp"

#include <iostream>
#include <cstdlib>
#include <atomic>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace re_wor::memory {

static MemoryHeapConfig g_heap_config;
static std::atomic<size_t> g_allocated_bytes{0};

void initialize_memory_heaps(rex::Runtime* runtime, const MemoryHeapConfig& config) {
  (void)runtime;
  g_heap_config = config;

  TRACE("[MemoryHeaps] Initializing expanded 64-bit PC memory subsystem");
  TRACE("[MemoryHeaps]   Heap Multiplier:  " << g_heap_config.heap_multiplier << "x");
  TRACE("[MemoryHeaps]   SongHeap Limit:   " << g_heap_config.song_heap_mb << " MB");
  TRACE("[MemoryHeaps]   Max Memory Cap:   " << g_heap_config.max_total_heap_mb << " MB");
}

void* AllocateExpandedMemory(size_t size, size_t alignment) {
  if (size == 0) return nullptr;

  size_t expanded_size = size * g_heap_config.heap_multiplier;
  g_allocated_bytes += expanded_size;

#ifdef _WIN32
  void* ptr = _aligned_malloc(expanded_size, alignment < 16 ? 16 : alignment);
#else
  void* ptr = nullptr;
  posix_memalign(&ptr, alignment < 16 ? 16 : alignment, expanded_size);
#endif

  return ptr;
}

void FreeExpandedMemory(void* ptr) {
  if (!ptr) return;
#ifdef _WIN32
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

}  // namespace re_wor::memory

