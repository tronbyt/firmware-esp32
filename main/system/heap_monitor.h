#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t internal_free;
    size_t internal_min;
    size_t internal_largest_block;
    size_t spiram_free;
    size_t spiram_min;
    size_t spiram_largest_block;
    size_t dma_free;
} heap_snapshot_t;

/** Initialize heap monitoring and record baseline. Call early in app_main. */
void heap_monitor_init(void);

/** Log current heap status with per-capability breakdown. */
void heap_monitor_log_status(const char* tag);

/** Take a checkpoint for later delta comparison. */
void heap_monitor_checkpoint(const char* label);

/** Log delta since last checkpoint. */
void heap_monitor_check_since_checkpoint(const char* label);

/** Fill snapshot with current heap state. */
void heap_monitor_get_snapshot(heap_snapshot_t* snapshot);

/** Run heap integrity check; returns true if OK. */
bool heap_monitor_check_integrity(const char* location);

/** Dump detailed per-region heap info to log. */
void heap_monitor_dump_info(void);
