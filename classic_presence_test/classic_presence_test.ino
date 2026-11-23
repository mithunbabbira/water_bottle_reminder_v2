// Isolated Bluetooth Classic presence experiment for ORIGINAL ESP32-WROOM.
// NOT the product firmware. No scale, LEDs, buzzer, Wi-Fi, or bottle NVS writes.
// USB 115200: probe AA:BB:CC:DD:EE:FF | status | help
// BLE advertises "bottle-presence-lab" concurrently to exercise dual-mode radio.
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_gap_bt_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

#if !defined(CONFIG_BT_CLASSIC_ENABLED)
#error "This experiment requires Bluetooth Classic on the original ESP32, not C3/S3."
#endif

struct NameResult {
  uint8_t mac[6];
  int status;
  char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
};
QueueHandle_t results;
bool pending = false, expired = false;
uint8_t target[6] = {};
uint32_t started = 0;
char input[96];
size_t inputLength = 0;
bool discard = false;

void classicEvent(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  // Inquiry output is for this isolated lab only; no bottle state is modified.
  if (event == ESP_BT_GAP_DISC_RES_EVT) {
    char name[65] = {};
    for (int i = 0; i < param->disc_res.num_prop; ++i) {
      auto &p = param->disc_res.prop[i];
      if (p.type == ESP_BT_GAP_DEV_PROP_BDNAME) {
        memcpy(name,p.val,min(size_t(p.len),sizeof(name)-1));
      } else if (p.type == ESP_BT_GAP_DEV_PROP_EIR && !name[0]) {
        uint8_t len = 0;
        uint8_t *value = esp_bt_gap_resolve_eir_data((uint8_t *)p.val,ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,&len);
        if (!value) value = esp_bt_gap_resolve_eir_data((uint8_t *)p.val,ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME,&len);
        if (value) memcpy(name,value,min(size_t(len),sizeof(name)-1));
      }
    }
    const uint8_t *b = param->disc_res.bda;
    Serial.printf("found %02X:%02X:%02X:%02X:%02X:%02X name=%s\n",b[0],b[1],b[2],b[3],b[4],b[5],name[0] ? name : "(name pending)");
    return;
  }
  if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
    Serial.printf("discovery=%s\n",param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED ? "running" : "stopped");
    return;
  }
  // Remote-name completions are copied into a bounded queue.
  if (event != ESP_BT_GAP_READ_REMOTE_NAME_EVT) return;
  NameResult r = {};
  memcpy(r.mac, param->read_rmt_name.bda, sizeof(r.mac));
  r.status = param->read_rmt_name.stat;
  memcpy(r.name, param->read_rmt_name.rmt_name, sizeof(r.name)-1);
  xQueueSend(results, &r, 0);
}

bool parseAddress(const char *text, uint8_t mac[6]) {
  unsigned b[6]; int end = 0;
  if (strlen(text) != 17 || sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%n",
      &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &end) != 6 || end != 17) return false;
  bool nonzero = false;
  for (int i = 0; i < 6; ++i) { mac[i] = uint8_t(b[i]); nonzero |= b[i] != 0; }
  return nonzero;
}

void command(const char *line) {
  if (!strcmp(line,"help")) {
    Serial.println("scan | probe AA:BB:CC:DD:EE:FF | status | help");
    Serial.println("Use the phone's CLASSIC address, not a rotating BLE peer address.");
  } else if (!strcmp(line,"status")) {
    Serial.printf("probe=%s heap=%u uptime_ms=%lu\n", pending ? (expired ? "timed_out_waiting_for_callback" : "pending") : "idle",
                  ESP.getFreeHeap(), (unsigned long)millis());
  } else if (!strcmp(line,"scan")) {
    if (pending) { Serial.println("busy: finish probe first"); return; }
    const esp_err_t rc = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,8,0);
    Serial.printf("discovery request: %s\n",esp_err_to_name(rc));
  } else if (!strncmp(line,"probe ",6)) {
    if (pending) { Serial.println("busy: wait for completion; reset test firmware if the controller never completes"); return; }
    uint8_t address[6];
    if (!parseAddress(line+6,address)) { Serial.println("invalid address; use exact XX:XX:XX:XX:XX:XX"); return; }
    memcpy(target,address,6); pending = true; expired = false; started = millis();
    const esp_err_t err = esp_bt_gap_read_remote_name(target);
    if (err != ESP_OK) { pending = false; Serial.printf("probe could not start: %s\n",esp_err_to_name(err)); return; }
    Serial.println("probe started (not a presence result)");
  } else Serial.println("unknown command; type help");
}

void setup() {
  // Keep the existing WROOM base quiet during the lab (common-anode RGB).
  for (int pin : {12,14,27}) { pinMode(pin,OUTPUT); digitalWrite(pin,HIGH); }
  for (int pin : {25,26}) { pinMode(pin,OUTPUT); digitalWrite(pin,LOW); }
  Serial.begin(115200);
  results = xQueueCreate(4,sizeof(NameResult));
  if (!results) { Serial.println("result queue allocation failed"); return; }
  if (!BLEDevice::init("bottle-presence-lab")) { Serial.println("Bluetooth initialization failed"); return; }
  const esp_err_t rc = esp_bt_gap_register_callback(classicEvent);
  if (rc != ESP_OK) { Serial.printf("Classic callback failed: %s\n",esp_err_to_name(rc)); return; }
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService("6b6f7474-6c65-40f1-b5a3-000000000001");
  service->createCharacteristic("6b6f7474-6c65-40f2-b5a3-000000000001",BLECharacteristic::PROPERTY_READ)->setValue("presence experiment only");
  service->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(service->getUUID()); adv->setScanResponse(true); BLEDevice::startAdvertising();
  Serial.println("Classic presence experiment ready. BLE advertising concurrently. No target saved or probed automatically.");
  command("help");
}

void loop() {
  if (!results) { delay(10); return; }
  NameResult r;
  while (xQueueReceive(results,&r,0) == pdTRUE) {
    if (!pending || memcmp(r.mac,target,6)) { Serial.println("ignored unmatched callback"); continue; }
    const bool onTime = !expired && millis()-started < 20000;
    // No cached-name/database fallback. A positive completion still needs the
    // Bluetooth-OFF acceptance test to verify real reachability on each phone.
    Serial.printf("result=%s status=%d elapsed_ms=%lu name_bytes=%u\n",
      !onTime ? "late_not_accepted" : r.status == ESP_BT_STATUS_SUCCESS ? "remote_name_reply" : "no_reply_or_error",
      r.status,(unsigned long)(millis()-started),(unsigned)strlen(r.name));
    pending = false;
  }
  if (pending && !expired && millis()-started >= 20000) {
    expired = true;
    Serial.println("result=timeout_not_present; awaiting controller completion before another request");
  }
  for (int n = 0; n < 64 && Serial.available(); ++n) {
    const char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (!discard && inputLength) { input[inputLength] = 0; command(input); }
      else if (discard) Serial.println("input too long or invalid");
      inputLength = 0; discard = false;
    } else if (!ch || inputLength == sizeof(input)-1) discard = true;
    else if (!discard) input[inputLength++] = ch;
  }
  delay(1);
}
