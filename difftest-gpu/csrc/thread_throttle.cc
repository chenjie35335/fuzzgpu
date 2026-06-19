#include "difftest_gpu/util/thread_throttle.hh"

namespace difftest {

/**
 * @return true if not terminated, false if terminated
 */
bool ThreadThrottle::poll() {
  // If pause requested, enter a blocking wait. We only set the ack flag
  // while we are about to block so the pauser can observe that we are
  // paused. Wake-up occurs on resume or termination.
  if (paused.load(std::memory_order_acquire)) {
    pause_acknowledged.store(true, std::memory_order_release);
    std::unique_lock<std::mutex> lk(pause_mutex);
    pause_cv.wait(lk, [&] {
      return !paused.load(std::memory_order_acquire) ||
             terminated.load(std::memory_order_acquire);
    });
    // cleared ack once resumed
    pause_acknowledged.store(false, std::memory_order_release);

  } else {
    // No pause; yield so this loop doesn't aggressively busy-spin when
    // there are no events. This keeps latency low while being CPU
    // friendly.
    std::this_thread::yield();
  }
  return !terminated.load(std::memory_order_acquire);
}

void ThreadThrottle::pause() {
  // Request pause
  paused.store(true, std::memory_order_release);

  // Wait until the emulator thread acknowledges it is paused. Use a
  // short spin with yields — this is low-latency and avoids additional
  // condition variables complexity on the caller side.
  for (int i = 0; i < PAUSE_ACK_SHORT_SPIN; ++i) {
    if (pause_acknowledged.load(std::memory_order_acquire))
      return;
    std::this_thread::yield();
  }

  // Fallback: if not acknowledged yet, sleep briefly in case the worker
  // is preempted. This still prioritizes responsiveness.
  while (!pause_acknowledged.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(PAUSE_ACK_BACKOFF_SLEEP);
  }
}

void ThreadThrottle::resume() {
  // Clear pause and notify the worker. Then wait until the worker clears
  // the acknowledgement flag so callers know execution has resumed.
  paused.store(false, std::memory_order_release);
  pause_cv.notify_one();

  // Wait for ack to clear.
  for (int i = 0; i < PAUSE_ACK_SHORT_SPIN; ++i) {
    if (!pause_acknowledged.load(std::memory_order_acquire))
      return;
    std::this_thread::yield();
  }
  while (pause_acknowledged.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(PAUSE_ACK_BACKOFF_SLEEP);
  }
}

} // namespace difftest