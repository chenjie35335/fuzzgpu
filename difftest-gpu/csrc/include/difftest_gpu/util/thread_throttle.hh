#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace difftest {

class ThreadThrottle {

  // Pause control for the emulator thread
  std::atomic<bool> terminated{false};
  std::atomic<bool> paused{false};
  // Indicates the worker has acknowledged entering the paused state.
  std::atomic<bool> pause_acknowledged{false};
  std::mutex pause_mutex;
  std::condition_variable pause_cv;

  // Tuning parameters for pause/resume handshake. These are chosen to
  // balance latency (fast path) with CPU efficiency (fallback when the
  // worker thread is preempted). They can be adjusted per-platform if
  // needed.
  static constexpr int PAUSE_ACK_SHORT_SPIN =
      100000; // short spin+yield attempts
  static constexpr auto PAUSE_ACK_BACKOFF_SLEEP = std::chrono::microseconds(50);

public:
  bool poll();

  void terminate() { terminated.store(true, std::memory_order_release); }

  void pause();

  void resume();
};

} // namespace difftest