#pragma once
#include "esp_bt.h"
enum esp_bluedroid_status_t { ESP_BLUEDROID_STATUS_UNINITIALIZED, ESP_BLUEDROID_STATUS_ENABLED };
namespace NativeBt { inline bool &bluedroid() { static bool b = false; return b; } }
inline esp_bluedroid_status_t esp_bluedroid_get_status() {
  return NativeBt::bluedroid() ? ESP_BLUEDROID_STATUS_ENABLED : ESP_BLUEDROID_STATUS_UNINITIALIZED;
}
