#pragma once

#include <rex/ppc.h>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <fstream>
#include <chrono>
#include <cstdint>
#include <iomanip>

namespace re_wor::diagnostics {

struct StubEntry {
  const char* name;
  std::atomic<uint64_t>* counter;
};

struct UnmappedEntry {
  uint64_t count;
  uint32_t first_caller;
};

class StubTracker {
 public:
  static StubTracker& instance() {
    static StubTracker tracker;
    return tracker;
  }

  std::atomic<uint64_t>* register_stub(const char* name) {
    auto* counter = new std::atomic<uint64_t>(0);
    std::lock_guard<std::mutex> lock(stub_mutex_);
    stubs_.push_back({name, counter});
    return counter;
  }

  void record_unmapped(uint32_t target, uint32_t caller) {
    std::lock_guard<std::mutex> lock(unmapped_mutex_);
    auto& entry = unmapped_[target];
    entry.count++;
    if (entry.first_caller == 0) {
      entry.first_caller = caller;
    }
  }

  void dump_report() {
    std::ofstream out("re_wor_stub_freq.log");
    if (!out.is_open()) return;

    out << "=== RE:WOR STUB & UNMAPPED FUNCTION REPORT ===\n\n";

    std::vector<std::pair<const char*, uint64_t>> active_stubs;
    {
      std::lock_guard<std::mutex> lock(stub_mutex_);
      for (auto& s : stubs_) {
        uint64_t val = s.counter->load(std::memory_order_relaxed);
        if (val > 0) active_stubs.push_back({s.name, val});
      }
    }
    std::sort(active_stubs.begin(), active_stubs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    out << "--- ACTIVE STUBS (called > 0 times) ---\n";
    for (const auto& s : active_stubs) {
      out << std::setw(8) << s.second << " calls: " << s.first << "\n";
    }

    out << "\n--- UNMAPPED PPC TARGETS ---\n";
    {
      std::lock_guard<std::mutex> lock(unmapped_mutex_);
      for (const auto& kv : unmapped_) {
        out << "  Target: 0x" << std::hex << kv.first
            << " Count: " << std::dec << kv.second.count
            << " First caller: 0x" << std::hex << kv.second.first_caller << "\n";
      }
    }
    out.flush();
  }

  void start_reporter(int interval_seconds = 5) {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this, interval_seconds]() {
      while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
        dump_report();
      }
    });
    worker_.detach();
  }

 private:
  StubTracker() = default;
  ~StubTracker() { running_ = false; }

  std::mutex stub_mutex_;
  std::vector<StubEntry> stubs_;
  std::mutex unmapped_mutex_;
  std::unordered_map<uint32_t, UnmappedEntry> unmapped_;
  std::atomic<bool> running_{false};
  std::thread worker_;
};

void initialize_diagnostics();

}  // namespace re_wor::diagnostics
