#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <vector>

struct FakeNvs {
  std::vector<uint8_t> blob;
  bool exists = false;
  bool wrong_type = false;
  bool write_fails = false;
  esp_err_t read_error = ESP_OK;
  unsigned writes = 0;

  void reset() {
    blob.clear();
    exists = wrong_type = write_fails = false;
    read_error = ESP_OK;
    writes = 0;
  }
};

extern FakeNvs fake_nvs;
