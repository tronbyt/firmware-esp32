#include "console.h"

#include "sdkconfig.h"

#ifdef CONFIG_ENABLE_CONSOLE

#include <cassert>
#include <cinttypes>
#include <cstdio>

#include <esp_app_desc.h>
#include <esp_console.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif

namespace {

int cmd_free(int argc, char** argv) {
  printf("internal: %" PRIu32 " total: %" PRIu32 "\n",
         esp_get_free_internal_heap_size(), esp_get_free_heap_size());
  return 0;
}

int cmd_heap(int argc, char** argv) {
  uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t free_external = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint32_t min_internal =
      heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

  printf("free_internal: %" PRIu32 "\n", free_internal);
  printf("free_external: %" PRIu32 "\n", free_external);
  printf("internal_watermark: %" PRIu32 "\n", min_internal);
  return 0;
}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
int cmd_task_dump(int argc, char** argv) {
  UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
  auto* task_array = static_cast<TaskStatus_t*>(
      malloc(num_tasks * sizeof(TaskStatus_t)));
  if (!task_array) {
    printf("error: failed to allocate task array\n");
    return 1;
  }

  uint32_t total_runtime;
  num_tasks = uxTaskGetSystemState(task_array, num_tasks, &total_runtime);

  printf("%-16s %5s %5s %10s\n", "Name", "State", "Prio", "Stack");
  printf("%-16s %5s %5s %10s\n", "----", "-----", "----", "-----");

  for (UBaseType_t i = 0; i < num_tasks; i++) {
    const char* state;
    switch (task_array[i].eCurrentState) {
      case eRunning:
        state = "RUN";
        break;
      case eReady:
        state = "RDY";
        break;
      case eBlocked:
        state = "BLK";
        break;
      case eSuspended:
        state = "SUS";
        break;
      case eDeleted:
        state = "DEL";
        break;
      default:
        state = "???";
        break;
    }
    printf("%-16s %5s %5u %10u\n", task_array[i].pcTaskName, state,
           static_cast<unsigned>(task_array[i].uxCurrentPriority),
           static_cast<unsigned>(task_array[i].usStackHighWaterMark));
  }

  free(task_array);
  return 0;
}
#endif

int cmd_version(int argc, char** argv) {
  const esp_app_desc_t* app = esp_app_get_description();

  printf("{\n");
  printf("  \"project_name\": \"%s\",\n", app->project_name);
  printf("  \"version\": \"%s\",\n", app->version);
  printf("  \"compile_time\": \"%s\",\n", app->time);
  printf("  \"compile_date\": \"%s\",\n", app->date);
  printf("  \"idf_version\": \"%s\"\n", app->idf_ver);
  printf("}\n");

  return 0;
}

int cmd_assert(int argc, char** argv) {
  printf("Triggering system crash...\n");
  assert(0);
  return 0;
}

void register_commands() {
  esp_console_register_help_command();

  const esp_console_cmd_t cmds[] = {
      {.command = "free",
       .help = "Get free heap memory",
       .hint = nullptr,
       .func = &cmd_free,
       .argtable = nullptr,
       .func_w_context = nullptr,
       .context = nullptr},
      {.command = "heap",
       .help = "Get heap statistics (internal, external, watermark)",
       .hint = nullptr,
       .func = &cmd_heap,
       .argtable = nullptr,
       .func_w_context = nullptr,
       .context = nullptr},
      {.command = "version",
       .help = "Get firmware version information",
       .hint = nullptr,
       .func = &cmd_version,
       .argtable = nullptr,
       .func_w_context = nullptr,
       .context = nullptr},
      {.command = "assert",
       .help = "Crash the system for testing",
       .hint = nullptr,
       .func = &cmd_assert,
       .argtable = nullptr,
       .func_w_context = nullptr,
       .context = nullptr},
  };

  for (const auto& cmd : cmds) {
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
  }

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
  const esp_console_cmd_t task_cmd = {
      .command = "task_dump",
      .help = "Print task list (name, state, priority, stack HWM)",
      .hint = nullptr,
      .func = &cmd_task_dump,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&task_cmd));
#endif
}

}  // namespace

void console_init(void) {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
  if (!usb_serial_jtag_is_connected()) {
    return;
  }

  esp_console_repl_t* repl = nullptr;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = "tty>";

  register_commands();

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  esp_console_dev_usb_serial_jtag_config_t hw_config =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(
      esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#else
  esp_console_dev_uart_config_t hw_config =
      ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(
      esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#endif
  ESP_ERROR_CHECK(esp_console_start_repl(repl));
#endif
}

#else  // !CONFIG_ENABLE_CONSOLE

void console_init(void) {}

#endif
