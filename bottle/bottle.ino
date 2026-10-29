// Scale plus boot checks. Serial 115200.
// t = save the empty base as zero
// c 400 = the object on the scale weighs 400 g
// w = print the weight now
// p = advertise as Bottle so the phone can pair once
// Boot prints weight, zero, calibration, phone, Wi-Fi, time, and the from–until window.
// It does not write a new zero, open a setup network, or start reminders.

#include "PhonePresence.h"
#include "Portal.h"
#include "Scale.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <WiFi.h>
#include <host/ble_store.h>
#include <mbedtls/aes.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

BottleNvm nvm;
Scale scale;
Outputs outputs;
Hydration hydration;
Clock wallClock;
Portal portal;

char line[128];
uint8_t lineLen = 0;
uint32_t lastPrintMs = 0;

struct Heard {
  char name[25];
  char mac[18];
  int rssi;
};
Heard heard[8];
int heardCount = 0;

bool pairing = false;
uint32_t pairingMs = 0;
static const uint32_t PAIRING_WINDOW_MS = 120000;
bool serverReady = false;

struct PairResult {
  int status;
  bool hasIrk;
  bool hasMac;
  bool sameSaved;
  bool replaced;
  uint8_t irk[16];
  uint8_t mac[6];
};
PairResult pairResult;
volatile bool pairReady = false;
volatile bool phoneConnected = false;
volatile unsigned dropCount = 0;
volatile uint32_t phoneSeenMs = 0;
volatile bool phoneEverSeen = false;

static const uint32_t WEIGHT_WAIT_MS = 2000;

PhonePresence presence;

bool parseMac(const char *text, uint8_t out[6]) {
  unsigned int b[6];
  if (sscanf(text, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) {
    if (b[i] > 255) {
      return false;
    }
    out[i] = (uint8_t)b[i];
  }
  return true;
}

class NameScan : public BLEAdvertisedDeviceCallbacks {
 public:
  void onResult(BLEAdvertisedDevice advertisedDevice) override { (void)advertisedDevice; }
};

bool addressEmpty(const ble_addr_t &addr) {
  for (int i = 0; i < 6; i++) {
    if (addr.val[i] != 0) {
      return false;
    }
  }
  return true;
}

void copyPrintedMac(const ble_addr_t &addr, uint8_t out[6]) {
  for (int i = 0; i < 6; i++) {
    out[i] = addr.val[5 - i];
  }
}

void forgetPeer(const uint8_t printed[6]) {
  ble_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  for (int i = 0; i < 6; i++) {
    addr.val[i] = printed[5 - i];
  }
  addr.type = BLE_ADDR_PUBLIC;
  ble_store_util_delete_peer(&addr);
  addr.type = BLE_ADDR_RANDOM;
  ble_store_util_delete_peer(&addr);
}

bool isSavedPhone(const uint8_t mac[6]) {
  return nvm.data.hasBleMac && memcmp(mac, nvm.data.bleMac, 6) == 0;
}

class PairCallbacks : public BLESecurityCallbacks {
 public:
  void onAuthenticationComplete(ble_gap_conn_desc *desc, int status) override {
    pairResult.status = status;
    pairResult.hasIrk = false;
    pairResult.hasMac = false;
    pairResult.sameSaved = false;
    pairResult.replaced = false;
    if (desc != nullptr) {
      ble_addr_t id = desc->peer_id_addr;
      if (addressEmpty(id)) {
        id = desc->peer_ota_addr;
      }
      if (!addressEmpty(id)) {
        copyPrintedMac(id, pairResult.mac);
        pairResult.hasMac = true;
        pairResult.sameSaved = isSavedPhone(pairResult.mac);
      }
      if (status == 0) {
        struct ble_store_key_sec key;
        struct ble_store_value_sec value;
        memset(&key, 0, sizeof(key));
        key.peer_addr = desc->peer_id_addr;
        int rc = ble_store_read_peer_sec(&key, &value);
        if (rc != 0 || !value.irk_present) {
          memset(&key, 0, sizeof(key));
          key.peer_addr = desc->peer_ota_addr;
          rc = ble_store_read_peer_sec(&key, &value);
        }
        if (rc == 0 && value.irk_present) {
          memcpy(pairResult.irk, value.irk, sizeof(pairResult.irk));
          pairResult.hasIrk = true;
          if (nvm.data.hasBleMac && !pairResult.sameSaved) {
            forgetPeer(nvm.data.bleMac);
            pairResult.replaced = true;
          }
        } else if (pairResult.hasMac && !pairResult.sameSaved) {
          forgetPeer(pairResult.mac);
        }
      } else if (pairResult.hasMac && !pairResult.sameSaved) {
        forgetPeer(pairResult.mac);
      }
    }
    __sync_synchronize();
    pairReady = true;
  }
};

class LinkCallbacks : public BLEServerCallbacks {
 public:
  void onConnect(BLEServer *server, ble_gap_conn_desc *desc) override {
    (void)server;
    (void)desc;
    phoneConnected = true;
    phoneSeenMs = millis();
    phoneEverSeen = true;
  }

  void onDisconnect(BLEServer *server, ble_gap_conn_desc *desc) override {
    (void)desc;
    phoneConnected = server != nullptr && server->getConnectedCount() > 0;
    if (!phoneConnected) {
      __sync_fetch_and_add(&dropCount, 1);
    }
  }
};

bool ensureBle() {
  if (BLEDevice::getInitialized()) {
    return true;
  }
  return BLEDevice::init("Bottle");
}

bool samePhone(const uint8_t *native) {
  if (!nvm.data.hasIrk || native == nullptr) {
    return false;
  }
  uint8_t printed[6];
  for (int i = 0; i < 6; i++) {
    printed[i] = native[5 - i];
  }
  if (nvm.data.hasBleMac && memcmp(printed, nvm.data.bleMac, 6) == 0) {
    return true;
  }
  if ((native[5] & 0xC0) != 0x40) {
    return false;
  }
  uint8_t key[16];
  uint8_t plain[16];
  uint8_t cipher[16];
  for (int i = 0; i < 16; i++) {
    key[i] = nvm.data.irk[15 - i];
  }
  memset(plain, 0, sizeof(plain));
  plain[13] = native[5];
  plain[14] = native[4];
  plain[15] = native[3];
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  const int keyRc = mbedtls_aes_setkey_enc(&aes, key, 128);
  const int cryptRc = keyRc == 0 ? mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, plain, cipher) : keyRc;
  mbedtls_aes_free(&aes);
  if (cryptRc != 0) {
    return false;
  }
  return cipher[15] == native[0] && cipher[14] == native[1] && cipher[13] == native[2];
}

void printMac(const uint8_t mac[6]) {
  for (int i = 0; i < 6; i++) {
    if (i > 0) {
      Serial.print(':');
    }
    if (mac[i] < 16) {
      Serial.print('0');
    }
    Serial.print(mac[i], HEX);
  }
}

void reportDrops() {
  const unsigned drops = __sync_lock_test_and_set(&dropCount, 0);
  if (drops == 0) {
    return;
  }
  Serial.print("phone disconnected");
  if (drops > 1) {
    Serial.print(' ');
    Serial.print(drops);
  }
  Serial.println();
}

bool phoneIsConnected() {
  if (phoneConnected) {
    phoneSeenMs = millis();
    phoneEverSeen = true;
    return true;
  }
  if (!serverReady || !BLEDevice::getInitialized()) {
    return false;
  }
  BLEServer *server = BLEDevice::getServer();
  const bool connected = server != nullptr && server->getConnectedCount() > 0;
  if (connected) {
    phoneSeenMs = millis();
    phoneEverSeen = true;
  }
  return connected;
}

AtHome currentAtHome() {
  AtHome home;
  const bool configured = nvm.data.hasIrk || nvm.data.hasBleMac;
  if (!configured) {
    home.watching = false;
    home.decided = true;
    home.home = true;
    return home;
  }
  presence.suspend(pairing);
  presence.tick(phoneIsConnected());
  home.watching = true;
  home.decided = true;
  home.home = presence.here();
  return home;
}

void reportPresence(const AtHome &home) {
  static int last = -1;
  const int state = !home.watching ? 0 : (home.home ? 1 : 2);
  if (state == last) {
    return;
  }
  last = state;
  if (state == 0) {
    Serial.println("presence: not watching, reminders allowed");
  } else if (state == 1) {
    Serial.println("presence: phone here, reminders allowed");
  } else {
    Serial.println("presence: phone away, reminders off");
  }
}

void finishPair() {
  if (!pairReady) {
    return;
  }
  __sync_synchronize();
  pairReady = false;
  if (pairResult.status != 0 || !pairResult.hasMac) {
    Serial.print("pair failed ");
    Serial.println(pairResult.status);
    return;
  }
  if (!pairResult.hasIrk || !nvm.saveIrk(pairResult.irk) || !nvm.saveBleMac(pairResult.mac)) {
    Serial.println("paired, phone did not share its identity key");
    return;
  }
  if (phoneIsConnected()) {
    BLEDevice::getAdvertising()->stop();
  }
  pairing = false;
  presence.setAddress(pairResult.mac);
  if (pairResult.sameSaved) {
    Serial.println("phone connected");
    return;
  }
  Serial.print("phone paired ");
  printMac(pairResult.mac);
  Serial.println();
  if (pairResult.replaced) {
    Serial.println("replaced previous phone");
  }
  Serial.println("phone: ok");
}

void startBleServer(bool announce) {
  if (announce) {
    Serial.println("pairing");
    Serial.println("On the phone, open Bluetooth and pair with Bottle");
  }
  if (!ensureBle()) {
    Serial.println("bluetooth failed to start");
    return;
  }
  BLEDevice::getScan()->stop();
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setInitEncryptionKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  BLESecurity::setRespEncryptionKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  static PairCallbacks callbacks;
  static LinkCallbacks linkCallbacks;
  BLEDevice::setSecurityCallbacks(&callbacks);
  if (!serverReady) {
    BLEServer *server = BLEDevice::createServer();
    BLEService *service = server->createService(BLEUUID((uint16_t)0x180A));
    BLECharacteristic *name = service->createCharacteristic(
        BLEUUID((uint16_t)0x2A00), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC);
    name->setValue("Bottle");
    service->start();
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLEUUID((uint16_t)0x180A));
    adv->setScanResponse(true);
    adv->setMinInterval(160);
    adv->setMaxInterval(320);
    adv->setName("Bottle");
    serverReady = true;
  }
  BLEServer *server = BLEDevice::getServer();
  server->setCallbacks(&linkCallbacks);
  server->advertiseOnDisconnect(true);
  if (phoneIsConnected()) {
    if (announce) {
      Serial.println("phone still connected");
    }
    return;
  }
  if (!BLEDevice::getAdvertising()->start()) {
    Serial.println("pair advertise failed");
    return;
  }
  if (announce) {
    pairing = true;
    pairingMs = millis();
  }
}

void startPairing() { startBleServer(true); }

void bindPresence() {
  if (!nvm.data.hasIrk && !nvm.data.hasBleMac) {
    return;
  }
  if (!BLEDevice::getInitialized() && !ensureBle()) {
    return;
  }
  uint8_t mac[6];
  bool found = false;
  ble_addr_t peers[3];
  int count = 0;
  if (ble_store_util_bonded_peers(peers, &count, 3) == 0) {
    int chosen = -1;
    for (int i = 0; i < count; i++) {
      struct ble_store_key_sec key;
      struct ble_store_value_sec value;
      memset(&key, 0, sizeof(key));
      key.peer_addr = peers[i];
      if (ble_store_read_peer_sec(&key, &value) != 0 || !value.irk_present || !nvm.data.hasIrk) {
        continue;
      }
      if (memcmp(value.irk, nvm.data.irk, 16) == 0) {
        chosen = i;
        break;
      }
    }
    if (chosen < 0 && count > 0) {
      chosen = 0;
    }
    if (chosen >= 0) {
      for (int i = 0; i < 6; i++) {
        mac[i] = peers[chosen].val[5 - i];
      }
      found = true;
    }
  }
  if (!found && nvm.data.hasBleMac) {
    memcpy(mac, nvm.data.bleMac, 6);
    found = true;
  }
  if (!found) {
    return;
  }
  presence.setAddress(mac);
  Serial.print("presence ping ");
  printMac(mac);
  Serial.println();
}

bool printableName(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  for (const char *p = text; *p; ++p) {
    if (*p < 32 || *p > 126) {
      return false;
    }
  }
  return true;
}

void copyField(char *dest, size_t destLen, const uint8_t *data, uint8_t length) {
  if (length >= destLen) {
    length = destLen - 1;
  }
  memcpy(dest, data, length);
  dest[length] = '\0';
}

void readNames(BLEAdvertisedDevice &device, char *full, char *shortName) {
  full[0] = '\0';
  shortName[0] = '\0';
  const uint8_t *payload = device.getPayload();
  const size_t len = device.getPayloadLength();
  size_t i = 0;
  while (payload != nullptr && i + 1 < len) {
    const uint8_t field = payload[i];
    if (field == 0 || i + 1 + field > len) {
      break;
    }
    const uint8_t type = payload[i + 1];
    const uint8_t *data = payload + i + 2;
    const uint8_t n = field - 1;
    if (type == 0x09) {
      copyField(full, 25, data, n);
    } else if (type == 0x08 && shortName[0] == '\0') {
      copyField(shortName, 25, data, n);
    }
    i += field + 1;
  }
}

bool nameMatches(const char *name, const char *query) {
  if (query == nullptr || query[0] == '\0') {
    return true;
  }
  char left[25];
  char right[48];
  strncpy(left, name, sizeof(left) - 1);
  left[sizeof(left) - 1] = '\0';
  strncpy(right, query, sizeof(right) - 1);
  right[sizeof(right) - 1] = '\0';
  for (char *p = left; *p; ++p) {
    *p = (char)tolower(*p);
  }
  for (char *p = right; *p; ++p) {
    *p = (char)tolower(*p);
  }
  return strstr(left, right) != nullptr;
}

void searchPhones(const char *query) {
  Serial.println("searching");
  if (!ensureBle()) {
    Serial.println("phone scan failed to start");
    heardCount = 0;
    return;
  }
  if (phoneIsConnected()) {
    Serial.println("phone still connected");
    heardCount = 0;
    return;
  }
  presence.suspend(true);
  const bool resumeAdvert = BLEDevice::getAdvertising()->isAdvertising();
  if (resumeAdvert) {
    BLEDevice::getAdvertising()->stop();
  }
  static NameScan nameScan;
  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&nameScan, false, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->clearResults();
  BLEScanResults *results = scan->start(8, false);
  heardCount = 0;
  bool phoneHere = false;
  int phoneRssi = 0;
  uint8_t wanted[6];
  const bool lookingForMac = query != nullptr && parseMac(query, wanted);
  int found = 0;
  if (results != nullptr) {
    found = results->getCount();
    for (int i = 0; i < found; i++) {
      BLEAdvertisedDevice device = results->getDevice(i);
      if (samePhone(device.getAddress().getNative())) {
        phoneHere = true;
        phoneRssi = device.getRSSI();
        phoneSeenMs = millis();
        phoneEverSeen = true;
      }
      if (!lookingForMac && heardCount >= 8) {
        continue;
      }
      String mac = device.getAddress().toString();
      mac.toLowerCase();
      if (mac.length() < 17) {
        continue;
      }
      char full[25];
      char shortName[25];
      readNames(device, full, shortName);
      const char *name = printableName(full) ? full : (printableName(shortName) ? shortName : nullptr);
      uint8_t seenMac[6];
      const bool macHit = lookingForMac && parseMac(mac.c_str(), seenMac) && memcmp(seenMac, wanted, 6) == 0;
      if (!macHit && (name == nullptr || !nameMatches(name, query))) {
        continue;
      }
      const char *label = name != nullptr ? name : "no name";
      strncpy(heard[heardCount].name, label, sizeof(heard[heardCount].name) - 1);
      heard[heardCount].name[sizeof(heard[heardCount].name) - 1] = '\0';
      strncpy(heard[heardCount].mac, mac.c_str(), sizeof(heard[heardCount].mac) - 1);
      heard[heardCount].mac[sizeof(heard[heardCount].mac) - 1] = '\0';
      heard[heardCount].rssi = device.getRSSI();
      heardCount++;
    }
  }
  if (resumeAdvert && !phoneIsConnected()) {
    BLEDevice::getAdvertising()->start();
  }
  presence.suspend(false);
  Serial.print("heard ");
  Serial.print(found);
  Serial.println(" devices");
  if (nvm.data.hasIrk) {
    if (phoneHere) {
      Serial.print("phone: here ");
      Serial.println(phoneRssi);
    } else {
      Serial.println("phone: not heard");
    }
  }
  if (heardCount == 0) {
    Serial.println(lookingForMac ? "not seen" : (query != nullptr && query[0] != '\0' ? "no phone with that name" : "no named phones heard"));
    return;
  }
  for (int i = 0; i < heardCount; i++) {
    Serial.print(i + 1);
    Serial.print(' ');
    Serial.print(heard[i].name);
    Serial.print(' ');
    Serial.print(heard[i].mac);
    Serial.print(' ');
    Serial.println(heard[i].rssi);
  }
  Serial.println("s <number> saves that phone");
}

String phonesJson() {
  searchPhones("");
  String json = "[";
  for (int i = 0; i < heardCount; i++) {
    if (i > 0) {
      json += ',';
    }
    json += "{\"name\":\"";
    for (const char *p = heard[i].name; *p; ++p) {
      if (*p == '"' || *p == '\\') {
        json += '\\';
      }
      json += *p;
    }
    json += "\",\"mac\":\"";
    json += heard[i].mac;
    json += "\"}";
  }
  json += ']';
  return json;
}

void saveHeard(int number) {
  if (number < 1 || number > heardCount) {
    Serial.println("search first with b, then s <number>");
    return;
  }
  uint8_t bytes[6];
  if (!parseMac(heard[number - 1].mac, bytes) || !nvm.saveBleMac(bytes)) {
    Serial.println("phone save failed");
    return;
  }
  Serial.print("phone saved ");
  Serial.println(heard[number - 1].mac);
}

void printHelp() {
  Serial.println("t zero the empty base");
  Serial.println("c <grams> calibrate from a known weight");
  Serial.println("p pair the phone. On the phone, open Bluetooth and tap Bottle");
  Serial.println("If that phone drops, Bottle advertises again");
  Serial.println("b search names, b ultra finds a name, s <number> saves");
  Serial.println("n scan 2.4 GHz Wi-Fi, j <ssid>|<pass> save Wi-Fi and join");
  Serial.println("r <ssid> forgets one saved Wi-Fi");
  Serial.println("a page status, w print weight, h help");
  Serial.println("i <seconds> reminder check until reboot, i 0 uses saved minutes");
  Serial.println("d clears today's total");
}

void handleLine() {
  if (lineLen == 0) {
    return;
  }
  line[lineLen] = '\0';
  const char cmd = line[0];
  if (cmd == 'h' || cmd == '?' || cmd == 'H') {
    printHelp();
  } else if (cmd == 't' || cmd == 'T') {
    if (scale.tare(nvm)) {
      hydration.forgetBaseline(nvm);
      Serial.print("zero saved ");
      Serial.println(nvm.data.tareOffset);
    } else {
      Serial.println("tare failed");
    }
  } else if (cmd == 'c' || cmd == 'C') {
    const float known = strtof(line + 1, nullptr);
    if (scale.calibrate(nvm, known)) {
      Serial.print("cal_factor ");
      Serial.println(nvm.data.calFactor, 4);
    } else {
      Serial.println("calibrate failed. Zero first, then c <grams>.");
    }
  } else if (cmd == 'p' || cmd == 'P') {
    startPairing();
  } else if (cmd == 'b' || cmd == 'B') {
    const char *query = line + 1;
    while (*query == ' ') {
      query++;
    }
    searchPhones(query);
  } else if (cmd == 's' || cmd == 'S') {
    saveHeard(atoi(line + 1));
  } else if (cmd == 'a' || cmd == 'A') {
    portal.printStatus();
  } else if (cmd == 'r' || cmd == 'R') {
    char *arg = line + 1;
    while (*arg == ' ') {
      arg++;
    }
    if (arg[0] == '\0' || !nvm.removeNet(arg)) {
      Serial.println("wifi forget failed");
    } else {
      Serial.print("forgot wifi ");
      Serial.println(arg);
      portal.restartRadio();
    }
  } else if (cmd == 'n' || cmd == 'N') {
    portal.printWifis();
  } else if (cmd == 'j' || cmd == 'J') {
    char *arg = line + 1;
    while (*arg == ' ') {
      arg++;
    }
    char *sep = strchr(arg, '|');
    if (sep == nullptr) {
      Serial.println("use: j <ssid>|<password>");
    } else {
      *sep = '\0';
      const char *pass = sep + 1;
      if (nvm.saveNet(arg, pass)) {
        Serial.print("saved wifi ");
        Serial.println(arg);
        portal.restartRadio();
      } else {
        Serial.println("wifi save failed (ssid <= 32, pass 8-63 or empty)");
      }
    }
  } else if (cmd == 'd' || cmd == 'D') {
    hydration.clearToday(nvm);
    Serial.println("today cleared");
  } else if (cmd == 'i' || cmd == 'I') {
    hydration.setCheckSeconds((uint16_t)atoi(line + 1), nvm);
  } else if (cmd == 'w' || cmd == 'W') {
    lastPrintMs = 0;
  } else {
    Serial.println("unknown. h for help");
  }
  lineLen = 0;
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      handleLine();
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}

bool waitForRaw() {
  const uint32_t start = millis();
  while (millis() - start < WEIGHT_WAIT_MS) {
    scale.poll(nvm.data);
    int32_t raw = 0;
    if (scale.latestRaw(&raw, WEIGHT_WAIT_MS)) {
      return true;
    }
    delay(10);
  }
  return false;
}

void printChecks(bool haveWeight) {
  if (!haveWeight) {
    Serial.println("weight: no data");
  } else if (scale.fault()) {
    Serial.println("weight: stuck at full scale, check load cell wires");
  } else {
    Serial.println("weight: ok");
  }
  Serial.println(nvm.data.hasTare ? "zero: ok" : "zero: not zeroed");
  Serial.println(nvm.data.hasCal ? "cal: ok" : "cal: not calibrated");
  if (nvm.data.hasIrk) {
    Serial.println("phone: ok");
  } else if (nvm.data.hasBleMac) {
    Serial.println("phone: saved address");
  } else {
    Serial.println("phone: no bluetooth address");
  }
}

bool insideWindow() {
  if (!wallClock.valid || nvm.data.startMin == nvm.data.endMin) {
    return false;
  }
  const uint16_t nowMin = (uint16_t)(wallClock.hour * 60 + wallClock.minute);
  if (nvm.data.startMin < nvm.data.endMin) {
    return nowMin >= nvm.data.startMin && nowMin < nvm.data.endMin;
  }
  return nowMin >= nvm.data.startMin || nowMin < nvm.data.endMin;
}

void printWindow() {
  if (!wallClock.valid) {
    Serial.println("window: no time");
    return;
  }
  Serial.println(insideWindow() ? "window: inside" : "window: outside");
}

void printTime() {
  if (!wallClock.valid) {
    Serial.println("time: no time");
    printWindow();
    return;
  }
  Serial.print("time: ");
  if (wallClock.hour < 10) {
    Serial.print('0');
  }
  Serial.print(wallClock.hour);
  Serial.print(':');
  if (wallClock.minute < 10) {
    Serial.print('0');
  }
  Serial.println(wallClock.minute);
  printWindow();
}

void printSample(uint32_t now) {
  if (now - lastPrintMs < 1000) {
    return;
  }
  lastPrintMs = now;
  int32_t raw = 0;
  if (!scale.latestRaw(&raw, 1000)) {
    Serial.println("scale: no data");
    return;
  }
  Serial.print("raw ");
  Serial.print(raw);
  float grams = 0.0f;
  if (scale.fault()) {
    Serial.println("  scale fault: check load cell wires");
  } else if (scale.latestGrams(&grams, 1000)) {
    Serial.print("  g ");
    Serial.println(grams, 1);
  } else {
    Serial.println("  (t to zero, then c <grams>)");
  }
}

void pumpIo() {
  scale.poll(nvm.data);
  portal.tick();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  outputs.begin();
  scale.begin();
  if (!nvm.begin()) {
    Serial.println("NVM open failed");
    return;
  }
  nvm.load();
  hydration.begin(nvm.data);
  hydration.setPump(pumpIo);
  Serial.println();
  Serial.println("scale");
  const bool haveWeight = waitForRaw();
  printChecks(haveWeight);
  portal.begin(&nvm, &scale, &hydration, &wallClock, phonesJson, currentAtHome);
  if (nvm.data.staSsid[0] == '\0') {
    Serial.println("wifi: no credentials");
    printTime();
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.println("wifi: connected");
    printTime();
  } else {
    Serial.println("wifi: not connected");
    printTime();
  }
  if (nvm.data.hasIrk) {
    startBleServer(false);
  }
  bindPresence();
  printHelp();
}

void loop() {
  scale.poll(nvm.data);
  portal.tick();
  pollSerial();
  reportDrops();
  finishPair();
  scale.poll(nvm.data);
  if (pairing && millis() - pairingMs > PAIRING_WINDOW_MS) {
    pairing = false;
  }
  const AtHome home = currentAtHome();
  reportPresence(home);
  hydration.tick(millis(), scale, nvm, outputs, wallClock, home);
  printSample(millis());
}
