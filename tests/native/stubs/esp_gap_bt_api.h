#pragma once
#include "esp_bt.h"
#include <stdint.h>
#include <cstring>
enum esp_bt_gap_cb_event_t { ESP_BT_GAP_READ_REMOTE_NAME_EVT, ESP_BT_GAP_DISC_STATE_CHANGED_EVT, ESP_BT_GAP_DISC_RES_EVT };
enum { ESP_BT_STATUS_SUCCESS, ESP_BT_STATUS_FAIL };
enum { ESP_BT_GAP_DISCOVERY_STARTED, ESP_BT_GAP_DISCOVERY_STOPPED };
enum { ESP_BT_GAP_DEV_PROP_BDNAME, ESP_BT_GAP_DEV_PROP_EIR, ESP_BT_GAP_DEV_PROP_RSSI };
enum { ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME = 9, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME = 8 };
static const int ESP_BT_INQ_MODE_GENERAL_INQUIRY = 0;
struct esp_bt_gap_dev_prop_t { int type; int len; void *val; };
struct esp_bt_gap_cb_param_t {
  struct { uint8_t bda[6]; int stat; uint8_t rmt_name[249]; } read_rmt_name;
  struct { int state; } disc_st_chg;
  struct { uint8_t bda[6]; int num_prop; esp_bt_gap_dev_prop_t *prop; } disc_res;
};
using esp_bt_gap_cb_t = void (*)(esp_bt_gap_cb_event_t, esp_bt_gap_cb_param_t *);
namespace NativeBt {
struct State { esp_bt_gap_cb_t callback = nullptr; int registration = 0, scanResult = 0, probeResult = 0;
  unsigned scanCalls = 0, probeCalls = 0, cancels = 0; uint8_t probed[6] = {}; };
inline State &state() { static State s; return s; }
}
inline esp_err_t esp_bt_gap_register_callback(esp_bt_gap_cb_t cb) {
  if (!NativeBt::state().registration) NativeBt::state().callback = cb;
  return NativeBt::state().registration;
}
inline esp_err_t esp_bt_gap_start_discovery(int mode, unsigned length, unsigned count) {
  if (mode != ESP_BT_INQ_MODE_GENERAL_INQUIRY || length != 8 || count != 0) return -1;
  ++NativeBt::state().scanCalls; return NativeBt::state().scanResult;
}
inline esp_err_t esp_bt_gap_cancel_discovery() { ++NativeBt::state().cancels; return ESP_OK; }
inline esp_err_t esp_bt_gap_read_remote_name(uint8_t *mac) {
  ++NativeBt::state().probeCalls; memcpy(NativeBt::state().probed, mac, 6); return NativeBt::state().probeResult;
}
