#include "heap_monitor.h"

#include <cstdint>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

namespace {

const char* TAG = "heap";

constexpr int32_t DRAM_WARNING_THRESHOLD = -4096;
constexpr int32_t SPIRAM_WARNING_THRESHOLD = -65536;

heap_snapshot_t s_baseline = {};
heap_snapshot_t s_checkpoint = {};
bool s_initialized = false;

void take_snapshot(heap_snapshot_t* snapshot) {
  snapshot->internal_free =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot->internal_min =
      heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot->internal_largest_block =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot->spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  snapshot->spiram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  snapshot->spiram_largest_block =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  snapshot->dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
}

}  // namespace

void heap_monitor_init(void) {
  if (s_initialized) {
    return;
  }
  s_initialized = true;

  take_snapshot(&s_baseline);
  s_checkpoint = s_baseline;

  ESP_LOGI(TAG, "Heap monitoring initialized");
  ESP_LOGI(TAG, "  DRAM:   free=%zu, min=%zu, blk=%zu",
           s_baseline.internal_free, s_baseline.internal_min,
           s_baseline.internal_largest_block);
  ESP_LOGI(TAG, "  SPIRAM: free=%zu, min=%zu, blk=%zu",
           s_baseline.spiram_free, s_baseline.spiram_min,
           s_baseline.spiram_largest_block);
  ESP_LOGI(TAG, "  DMA:    free=%zu", s_baseline.dma_free);

  heap_monitor_check_integrity("init");
}

void heap_monitor_log_status(const char* tag) {
  heap_snapshot_t now;
  take_snapshot(&now);

  auto delta_int = static_cast<int32_t>(now.internal_free) -
                   static_cast<int32_t>(s_baseline.internal_free);
  auto delta_spi = static_cast<int32_t>(now.spiram_free) -
                   static_cast<int32_t>(s_baseline.spiram_free);

  ESP_LOGI(TAG, "[%s] Free heap: %lu, min ever: %lu", tag,
           static_cast<unsigned long>(esp_get_free_heap_size()),
           static_cast<unsigned long>(esp_get_minimum_free_heap_size()));
  ESP_LOGI(TAG, "  DRAM:   free=%zu (%+ld since boot), min=%zu, blk=%zu",
           now.internal_free, static_cast<long>(delta_int), now.internal_min,
           now.internal_largest_block);
  ESP_LOGI(TAG, "  SPIRAM: free=%zu (%+ld since boot), min=%zu, blk=%zu",
           now.spiram_free, static_cast<long>(delta_spi), now.spiram_min,
           now.spiram_largest_block);
  ESP_LOGI(TAG, "  DMA:    free=%zu", now.dma_free);

  heap_monitor_check_integrity(tag);
}

void heap_monitor_checkpoint(const char* label) {
  take_snapshot(&s_checkpoint);
  ESP_LOGI(TAG, "[%s] Checkpoint: DRAM=%zu, SPIRAM=%zu", label,
           s_checkpoint.internal_free, s_checkpoint.spiram_free);
  heap_monitor_check_integrity(label);
}

void heap_monitor_check_since_checkpoint(const char* label) {
  heap_snapshot_t now;
  take_snapshot(&now);

  auto delta_int = static_cast<int32_t>(now.internal_free) -
                   static_cast<int32_t>(s_checkpoint.internal_free);
  auto delta_spi = static_cast<int32_t>(now.spiram_free) -
                   static_cast<int32_t>(s_checkpoint.spiram_free);

  ESP_LOGI(TAG, "[%s] Since checkpoint: DRAM %+ld (%zu), SPIRAM %+ld (%zu)",
           label, static_cast<long>(delta_int), now.internal_free,
           static_cast<long>(delta_spi), now.spiram_free);

  if (delta_int < DRAM_WARNING_THRESHOLD) {
    ESP_LOGW(TAG, "[%s] Significant DRAM drop: %+ld bytes", label,
             static_cast<long>(delta_int));
  }
  if (delta_spi < SPIRAM_WARNING_THRESHOLD) {
    ESP_LOGW(TAG, "[%s] Significant SPIRAM drop: %+ld bytes", label,
             static_cast<long>(delta_spi));
  }

  heap_monitor_check_integrity(label);
}

void heap_monitor_get_snapshot(heap_snapshot_t* snapshot) {
  if (snapshot) {
    take_snapshot(snapshot);
  }
}

bool heap_monitor_check_integrity(const char* location) {
  bool ok = heap_caps_check_integrity_all(true);
  if (!ok) {
    ESP_LOGE(TAG, "HEAP CORRUPTION detected at %s!", location);
  }
  return ok;
}

void heap_monitor_dump_info(void) {
  ESP_LOGI(TAG, "=== Detailed Heap Info (8-bit accessible) ===");
  heap_caps_print_heap_info(MALLOC_CAP_8BIT);
  ESP_LOGI(TAG, "=== Detailed Heap Info (Internal only) ===");
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  ESP_LOGI(TAG, "=== Detailed Heap Info (SPIRAM) ===");
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
}
