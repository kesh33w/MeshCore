#pragma once

#include "fake_nvs.h"
#include <string.h>

typedef int nvs_handle_t;
#define NVS_READONLY 1

inline esp_err_t nvs_open(const char* name, int, nvs_handle_t* handle) {
  if (strcmp(name, "ilink") != 0) return ESP_ERR_NVS_NOT_FOUND;
  *handle = 1;
  return fake_nvs.read_error;
}

inline esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* output,
                              size_t* length) {
  if (strcmp(key, "identity") != 0 || !fake_nvs.exists)
    return ESP_ERR_NVS_NOT_FOUND;
  if (fake_nvs.wrong_type) return ESP_ERR_NVS_TYPE_MISMATCH;
  if (*length < fake_nvs.blob.size()) return ESP_ERR_NVS_INVALID_LENGTH;
  memcpy(output, fake_nvs.blob.data(), fake_nvs.blob.size());
  *length = fake_nvs.blob.size();
  return ESP_OK;
}

inline void nvs_close(nvs_handle_t) {}
