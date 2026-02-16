#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Initialize the interactive UART console with diagnostic commands.
/// On S3 targets, starts a REPL over USB Serial JTAG.
/// No-op if CONFIG_ENABLE_CONSOLE is not set.
void console_init(void);

#ifdef __cplusplus
}
#endif
