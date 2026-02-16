#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace raii {

/**
 * RAII wrapper for FreeRTOS mutex/semaphore acquisition.
 * Acquires in constructor, releases in destructor.
 */
class MutexGuard {
 public:
  explicit MutexGuard(SemaphoreHandle_t mutex,
                      TickType_t timeout = portMAX_DELAY)
      : mutex_(mutex), acquired_(false) {
    if (mutex_) {
      acquired_ = (xSemaphoreTake(mutex_, timeout) == pdTRUE);
    }
  }

  ~MutexGuard() { release(); }

  explicit operator bool() const { return acquired_; }

  void release() {
    if (acquired_) {
      xSemaphoreGive(mutex_);
      acquired_ = false;
    }
  }

  // Non-copyable
  MutexGuard(const MutexGuard&) = delete;
  MutexGuard& operator=(const MutexGuard&) = delete;

 private:
  SemaphoreHandle_t mutex_;
  bool acquired_;
};

/**
 * RAII wrapper that gives a semaphore on destruction.
 * Useful for signaling completion when leaving a scope.
 */
class SemaphoreGiver {
 public:
  explicit SemaphoreGiver(SemaphoreHandle_t sem) : sem_(sem) {}

  ~SemaphoreGiver() {
    if (sem_) {
      xSemaphoreGive(sem_);
    }
  }

  void cancel() { sem_ = nullptr; }

  // Non-copyable
  SemaphoreGiver(const SemaphoreGiver&) = delete;
  SemaphoreGiver& operator=(const SemaphoreGiver&) = delete;

 private:
  SemaphoreHandle_t sem_;
};

}  // namespace raii
