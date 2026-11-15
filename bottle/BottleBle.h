#pragma once

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <atomic>
#include <deque>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_gatt_common_api.h>

static const char *BOTTLE_BLE_SERVICE = "6b6f7474-6c65-4001-b5a3-000000000001";
static const char *BOTTLE_BLE_CMD = "6b6f7474-6c65-4002-b5a3-000000000001";
static const char *BOTTLE_BLE_EVT = "6b6f7474-6c65-4003-b5a3-000000000001";

// Only queues cross the BLE-task / Arduino-loop boundary. Callbacks never touch
// scale, flash, time, Wi-Fi, outputs or the hydration engine. TX is framed and
// paced in loop() to work at the minimum ATT MTU, not silently truncate JSON.
class BottleBle {
 public:
  static constexpr size_t kMaxLine = 4096;
  struct Command { char text[kMaxLine + 1]; uint32_t session; };
  struct Link { bool connected; uint8_t mac[6]; uint32_t session; };

  bool begin() {
    commands_ = xQueueCreate(3, sizeof(Command));
    links_ = xQueueCreate(8, sizeof(Link));
    if (!commands_ || !links_) return false;
    BLEDevice::init("bottle");
    active_ = this;
    BLEDevice::setCustomGattsHandler(onGattEvent);
    server_ = BLEDevice::createServer();
    server_->setCallbacks(new ServerCallbacks(this));
    auto *svc = server_->createService(BOTTLE_BLE_SERVICE);
    auto *cmd = svc->createCharacteristic(BOTTLE_BLE_CMD, BLECharacteristic::PROPERTY_WRITE);
    cmd->setCallbacks(new CmdCallbacks(this));
    evt_ = svc->createCharacteristic(BOTTLE_BLE_EVT,
                                    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    subscription_ = new BLE2902();
    evt_->addDescriptor(subscription_);
    evt_->setCallbacks(new TxCallbacks(this));
    evt_->setValue("{\"protocol\":2}\n");
    svc->start();
    auto *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BOTTLE_BLE_SERVICE);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    return true;
  }

  bool connected() const { return connected_.load(); }
  uint32_t disconnectedAt() const { return disconnectedAt_.load(); }
  bool popLink(Link &link) { return xQueueReceive(links_, &link, 0) == pdTRUE; }
  bool popCommand(Command &cmd) {
    while (xQueueReceive(commands_, &cmd, 0) == pdTRUE) {
      if (connected() && cmd.session == session_.load()) return true;
    }
    return false;
  }
  uint32_t session() const { return session_.load(); }
  bool overflowed() { return overflow_.exchange(false); }
  bool txIdle() const { return tx_.empty(); }

  bool send(const String &line) {
    if (!connected()) return true;  // Serial-only clients need no BLE queue.
    if (line.length() > kMaxLine || txBytes_ + line.length() + 1 > 24000) return false;
    tx_.push_back(line + '\n');
    txBytes_ += line.length() + 1;
    return true;
  }

  void tick() {
    if (restartAdv_.exchange(false)) BLEDevice::startAdvertising();
    const auto currentSession = session_.load();
    if (!connected() || txSession_ != currentSession) {
      tx_.clear(); txBytes_ = 0; txOffset_ = 0;
      txSession_ = currentSession; txFailures_ = 0;
      return;
    }
    if (tx_.empty() || !subscription_->getNotifications() || congested_.load() ||
        esp_ble_get_cur_sendable_packets_num(server_->getConnId()) == 0 ||
        millis() - lastTxMs_ < (txFailures_ ? 50u : 20u)) return;
    String &front = tx_.front();
    const size_t count = min(size_t(20), front.length() - txOffset_);
    evt_->setValue(reinterpret_cast<const uint8_t *>(front.c_str() + txOffset_), count);
    txAccepted_ = false;
    evt_->notify(); // ESP32 core 3.3.12 invokes onStatus synchronously for admission.
    lastTxMs_ = millis();
    if (!txAccepted_.load()) {
      if (++txFailures_ >= 40) server_->disconnect(server_->getConnId());
      return; // retry the SAME fragment; never advance after GATT congestion
    }
    txFailures_ = 0;
    txOffset_ += count;
    if (txOffset_ == front.length()) {
      txBytes_ -= front.length(); tx_.pop_front(); txOffset_ = 0;
    }
  }

 private:
  BLEServer *server_ = nullptr;
  BLECharacteristic *evt_ = nullptr;
  BLE2902 *subscription_ = nullptr;
  QueueHandle_t commands_ = nullptr, links_ = nullptr;
  std::atomic<bool> connected_{false}, restartAdv_{false}, overflow_{false};
  std::atomic<uint32_t> session_{0}, disconnectedAt_{0};
  uint32_t rxSession_ = 0, txSession_ = 0;
  Command rx_ = {};
  size_t rxLength_ = 0;
  bool discarding_ = false;
  std::deque<String> tx_;
  size_t txBytes_ = 0, txOffset_ = 0;
  uint32_t lastTxMs_ = 0;
  uint8_t txFailures_ = 0;
  std::atomic<bool> txAccepted_{false}, congested_{false};
  inline static BottleBle *active_ = nullptr;
  static void onGattEvent(esp_gatts_cb_event_t event, esp_gatt_if_t, esp_ble_gatts_cb_param_t *param) {
    if (active_ && event == ESP_GATTS_CONGEST_EVT) active_->congested_ = param->congest.congested;
  }
  class TxCallbacks : public BLECharacteristicCallbacks {
   public:
    explicit TxCallbacks(BottleBle *owner) : o_(owner) {}
    void onStatus(BLECharacteristic *, Status s, uint32_t) override {
      o_->txAccepted_ = s == SUCCESS_NOTIFY;
    }
   private:
    BottleBle *o_;
  };

  class ServerCallbacks : public BLEServerCallbacks {
   public:
    explicit ServerCallbacks(BottleBle *owner) : o_(owner) {}
    void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override {
      // One owner link at a time. No advertising while connected.
      const uint32_t session = ++o_->session_;
      Link link = {true, {}, session};
      memcpy(link.mac, param->connect.remote_bda, 6);
      o_->congested_ = false;
      o_->connected_ = true;
      if (xQueueSend(o_->links_, &link, 0) != pdTRUE) server->disconnect(param->connect.conn_id);
    }
    void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *) override {
      o_->congested_ = false;
      o_->disconnectedAt_ = millis();
      o_->connected_ = false;
      const Link link = {false, {}, ++o_->session_};
      if (xQueueSend(o_->links_, &link, 0) != pdTRUE) o_->overflow_ = true;
      o_->restartAdv_ = true;
    }
   private:
    BottleBle *o_;
  };

  class CmdCallbacks : public BLECharacteristicCallbacks {
   public:
    explicit CmdCallbacks(BottleBle *owner) : o_(owner) {}
    void onWrite(BLECharacteristic *ch) override {
      const uint32_t session = o_->session_.load();
      if (o_->rxSession_ != session) {
        o_->rxSession_ = session; o_->rxLength_ = 0; o_->discarding_ = false;
      }
      const String value = ch->getValue();
      for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '\r') continue;
        if (c == '\n') {
          if (!o_->discarding_ && o_->rxLength_) {
            o_->rx_.text[o_->rxLength_] = '\0';
            o_->rx_.session = session;
            if (xQueueSend(o_->commands_, &o_->rx_, 0) != pdTRUE) o_->overflow_ = true;
          }
          // Commands contain credentials. Don't leave old copies in buffers.
          memset(o_->rx_.text, 0, o_->rxLength_);
          o_->rxLength_ = 0; o_->discarding_ = false;
        } else if (!o_->discarding_) {
          if (c == '\0' || o_->rxLength_ >= kMaxLine) {
            o_->discarding_ = true; o_->overflow_ = true;
          } else o_->rx_.text[o_->rxLength_++] = c;
        }
      }
    }
   private:
    BottleBle *o_;
  };
};
