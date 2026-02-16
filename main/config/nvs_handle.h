// C++ RAII wrapper for NVS operations
#pragma once

#include <esp_err.h>
#include <nvs_flash.h>

class NvsHandle {
 public:
  NvsHandle(const char* ns, nvs_open_mode_t mode)
      : handle_(0), open_err_(ESP_OK) {
    open_err_ = nvs_open(ns, mode, &handle_);
  }

  ~NvsHandle() {
    if (handle_ != 0) {
      nvs_close(handle_);
    }
  }

  NvsHandle(const NvsHandle&) = delete;
  NvsHandle& operator=(const NvsHandle&) = delete;

  explicit operator bool() const {
    return open_err_ == ESP_OK && handle_ != 0;
  }

  esp_err_t open_error() const { return open_err_; }

  esp_err_t commit() {
    if (!*this) return ESP_ERR_INVALID_STATE;
    return nvs_commit(handle_);
  }

  esp_err_t erase_key(const char* key) {
    if (!*this) return ESP_ERR_INVALID_STATE;
    return nvs_erase_key(handle_, key);
  }

  // Getters
  esp_err_t get_u8(const char* key, uint8_t* value) {
    if (!*this) return ESP_ERR_INVALID_STATE;
    return nvs_get_u8(handle_, key, value);
  }

  esp_err_t get_str(const char* key, char* buffer, size_t* len) {
    if (!*this) return ESP_ERR_INVALID_STATE;
    return nvs_get_str(handle_, key, buffer, len);
  }

  // Setters
  esp_err_t set_u8(const char* key, uint8_t value) {
    if (!*this) return ESP_ERR_INVALID_STATE;
    return nvs_set_u8(handle_, key, value);
  }

  esp_err_t set_str(const char* key, const char* value) {
    if (!*this) return ESP_ERR_INVALID_STATE;
    return nvs_set_str(handle_, key, value);
  }

 private:
  nvs_handle_t handle_;
  esp_err_t open_err_;
};
