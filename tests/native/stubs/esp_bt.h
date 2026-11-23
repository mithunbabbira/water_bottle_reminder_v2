#pragma once
using esp_err_t = int;
static const esp_err_t ESP_OK = 0;
enum esp_bt_controller_status_t { ESP_BT_CONTROLLER_STATUS_IDLE, ESP_BT_CONTROLLER_STATUS_ENABLED };
namespace NativeBt { inline bool &controller() { static bool b = false; return b; } }
inline esp_bt_controller_status_t esp_bt_controller_get_status() {
  return NativeBt::controller() ? ESP_BT_CONTROLLER_STATUS_ENABLED : ESP_BT_CONTROLLER_STATUS_IDLE;
}
