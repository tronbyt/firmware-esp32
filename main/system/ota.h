#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void run_ota(const char* url);
bool ota_in_progress(void);

#ifdef __cplusplus
}
#endif
