// Flash values for the bottle. Namespace "bottle". Legacy keys are read, never erased
// on migration. Each settings/network/hydration update is one atomic NVS blob write.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>
#include "ClassicText.h"

static const char *BOTTLE_NVS_NS = "bottle";
static const uint8_t BOTTLE_SCHEMA_VER = 2;
static const size_t BOTTLE_SSID_LEN = 33;
static const size_t BOTTLE_PASS_LEN = 65;
static const uint8_t BOTTLE_MAX_NETWORKS = 4;
static const uint8_t BEEP_OFF = 0;
static const uint8_t BEEP_SINGLE = 1;
static const uint8_t BEEP_DOUBLE = 2;
static const uint8_t BEEP_LONG = 3;

struct BottleNetwork {
  char ssid[BOTTLE_SSID_LEN];
  char pass[BOTTLE_PASS_LEN];
};

struct BottleNvmData {
  bool hasTare = false;
  int32_t tareOffset = 0;
  bool hasCal = false;
  float calFactor = 0;
  bool hasBaseline = false;
  float baselineWeight = 0;
  float dailyTotalMl = 0;
  int32_t lastDay = 0;
  bool hasBleMac = false;
  uint8_t bleMac[6] = {};
  bool hasClassicPhone = false;
  uint8_t classicMac[6] = {};
  char classicName[49] = {};
  bool hasIrk = false;
  uint8_t irk[16] = {};

  bool dnd = false;
  uint8_t colorR = 128, colorG = 0, colorB = 255;  // Idle, NOT reminder color.
  uint16_t startMin = 600, endMin = 1380, intervalMin = 30;
  uint8_t beepType = BEEP_DOUBLE;
  int16_t tzMin = 330;
  uint16_t missingSec = 180;
  uint8_t beepDelaySec = 5;
  uint16_t drinkGrams = 50, settleMs = 2000;
  uint32_t reminderColor = 0x00ffffu, missingColor = 0xff0000u;
  bool presenceEnabled = true; // read-only compatibility field; policy is always enforced

  char staSsid[BOTTLE_SSID_LEN] = {};
  char staPass[BOTTLE_PASS_LEN] = {};
  uint8_t netCount = 0;
  BottleNetwork nets[BOTTLE_MAX_NETWORKS] = {};
  char apSsid[BOTTLE_SSID_LEN] = {};
  char apPass[BOTTLE_PASS_LEN] = {};
  uint8_t apBehavior = 0;
};

class BottleNvm {
 public:
  BottleNvmData data;

  bool begin() { return prefs_.begin(BOTTLE_NVS_NS, false); }

  // Logical fields in blobs count as stored; untouched defaults do not. For
  // diagnostics of corrupt legacy values use stored() + settingUsable().
  bool stored(const char *key) {
    if (key == nullptr) return false;
    // Never interpret old aliases or BLE identities as a selected Classic target.
    if (strcmp(key, "classic_mac") == 0 || strcmp(key, "classic_name") == 0)
      return classicBlob_ && data.hasClassicPhone;
    if (isSetting(key) && settingsBlob_) return true;
    if (peerBlob_ && strcmp(key, "ble_mac") == 0) return data.hasBleMac;
    if (peerBlob_ && strcmp(key, "ble_irk") == 0) return data.hasIrk;
    if (hydrationBlob_ && (strcmp(key, "baseline_weight") == 0)) return data.hasBaseline;
    if (hydrationBlob_ && (strcmp(key, "daily_total") == 0 || strcmp(key, "last_day") == 0)) return true;
    if (networksBlob_) {
      if (strcmp(key, "net_count") == 0) return true;
      if (strcmp(key, "sta_ssid") == 0 || strcmp(key, "sta_pass") == 0) return data.netCount > 0;
      for (uint8_t i = 0; i < BOTTLE_MAX_NETWORKS; ++i) {
        char sk[12], pk[12];
        snprintf(sk, sizeof(sk), "wifi_s%u", i);
        snprintf(pk, sizeof(pk), "wifi_p%u", i);
        if (strcmp(key, sk) == 0 || strcmp(key, pk) == 0) return i < data.netCount;
      }
    }
    return prefs_.isKey(key);
  }
  bool rawStored(const char *key) { return key != nullptr && prefs_.isKey(key); }
  bool settingsStored() const { return settingsBlob_; }
  bool networksStored() const { return networksBlob_ || data.netCount > 0; }
  uint8_t storedSchemaVersion() { return prefs_.getUChar("schema", 0); }
  bool settingStored(const char *key) { return settingUsable(key) && stored(key); }
  bool settingUsable(const char *key) const {
    const int i = settingIndex(key);
    return i >= 0 && (settingsBlob_ || (legacyUsable_ & (1UL << i)) != 0);
  }

  void load() {
    data = BottleNvmData();
    settingsBlob_ = hydrationBlob_ = networksBlob_ = peerBlob_ = classicBlob_ = false;
    legacyUsable_ = 0;
    if (prefs_.getUChar("schema", 0) != BOTTLE_SCHEMA_VER) prefs_.putUChar("schema", BOTTLE_SCHEMA_VER);
    data.hasTare = prefs_.isKey("tare_offset");
    data.tareOffset = prefs_.getLong("tare_offset", 0);
    data.calFactor = prefs_.getFloat("cal_factor", 0);
    data.hasCal = prefs_.isKey("cal_factor") && validCal(data.calFactor);
    if (!data.hasCal) data.calFactor = 0;
    data.baselineWeight = prefs_.getFloat("baseline_weight", 0);
    data.hasBaseline = prefs_.isKey("baseline_weight") && validBaseline(data.baselineWeight);
    if (!data.hasBaseline) data.baselineWeight = 0;
    data.dailyTotalMl = prefs_.getFloat("daily_total", 0);
    if (!validTotal(data.dailyTotalMl)) data.dailyTotalMl = 0;
    data.lastDay = prefs_.getInt("last_day", 0);
    if (!validDay(data.lastDay)) data.lastDay = 0;
    loadHydration();
    data.hasBleMac = prefs_.getBytes("ble_mac", data.bleMac, sizeof(data.bleMac)) == sizeof(data.bleMac);
    if (!data.hasBleMac) memset(data.bleMac, 0, sizeof(data.bleMac));
    data.hasIrk = prefs_.getBytes("ble_irk", data.irk, sizeof(data.irk)) == sizeof(data.irk);
    if (!data.hasIrk) memset(data.irk, 0, sizeof(data.irk));
    loadPeer();
    loadClassic();

    loadLegacySettings();
    if (!loadSettings() && legacyUsable_ != 0) {
      // A fresh unit does NOT write defaults. Legacy migration is best effort;
      // failure leaves the original keys and their exact stored flags intact.
      saveSettings(data);
    }
    loadString("sta_ssid", data.staSsid, sizeof(data.staSsid));
    loadString("sta_pass", data.staPass, sizeof(data.staPass));
    loadNets();
    loadString("ap_ssid", data.apSsid, sizeof(data.apSsid));
    loadString("ap_pass", data.apPass, sizeof(data.apPass));
    data.apBehavior = prefs_.getUChar("ap_behavior", 0);
  }

  static bool validSettings(const BottleNvmData &d) {
    return d.startMin < 1440 && d.endMin < 1440 && d.intervalMin >= 1 && d.intervalMin <= 240 &&
           d.beepType <= BEEP_LONG && d.tzMin >= -720 && d.tzMin <= 840 && d.tzMin % 15 == 0 &&
           d.presenceEnabled && d.missingSec >= 10 && d.missingSec <= 3600 && d.beepDelaySec <= 30 &&
           d.drinkGrams >= 5 && d.drinkGrams <= 500 && d.settleMs >= 500 && d.settleMs <= 5000 &&
           d.reminderColor <= 0xffffffu && d.missingColor <= 0xffffffu;
  }

  // Applies ONLY settings from a snapshot. Calibration, totals, peers and nets
  // cannot be accidentally overwritten by an older command snapshot.
  bool saveSettings(const BottleNvmData &d) {
    if (!validSettings(d)) return false;
    uint8_t b[kSettingsBytes] = {};
    Writer w(b);
    w.u32(kSettingsMagic); w.u16(2);
    w.u8(d.dnd); w.u8(d.colorR); w.u8(d.colorG); w.u8(d.colorB);
    w.u16(d.startMin); w.u16(d.endMin); w.u16(d.intervalMin);
    w.u8(d.beepType); w.u16(static_cast<uint16_t>(d.tzMin));
    w.u16(d.missingSec); w.u8(d.beepDelaySec); w.u16(d.drinkGrams); w.u16(d.settleMs);
    w.u32(d.reminderColor); w.u32(d.missingColor); w.u8(d.presenceEnabled);
    w.u32(checksum(b, kSettingsBytes - 4));
    if (prefs_.putBytes("settings_v2", b, sizeof(b)) != sizeof(b)) return false;
    copySettings(data, d);
    settingsBlob_ = true;
    return true;
  }

  bool saveTare(int32_t offset) {
    if (prefs_.putLong("tare_offset", offset) != sizeof(int32_t)) return false;
    data.tareOffset = offset; data.hasTare = true;
    return true;
  }
  bool saveCal(float factor) {
    if (!validCal(factor) || prefs_.putFloat("cal_factor", factor) != sizeof(float)) return false;
    data.calFactor = factor; data.hasCal = true;
    return true;
  }
  bool saveHydration(bool hasBaseline, float grams, float total, int32_t day) {
    if (!isfinite(grams) || grams < 0 || (hasBaseline && !validBaseline(grams)) || !validTotal(total) || !validDay(day)) return false;
    uint8_t b[kHydrationBytes] = {};
    Writer w(b);
    w.u32(kHydrationMagic); w.u8(hasBaseline); w.f32(hasBaseline ? grams : 0);
    w.f32(total); w.u32(static_cast<uint32_t>(day));
    w.u32(checksum(b, kHydrationBytes - 4));
    if (prefs_.putBytes("hydration_v2", b, sizeof(b)) != sizeof(b)) return false;
    data.hasBaseline = hasBaseline; data.baselineWeight = hasBaseline ? grams : 0;
    data.dailyTotalMl = total; data.lastDay = day; hydrationBlob_ = true;
    return true;
  }
  bool saveBaseline(float grams) { return saveHydration(true, grams, data.dailyTotalMl, data.lastDay); }
  bool clearBaseline() { return saveHydration(false, 0, data.dailyTotalMl, data.lastDay); }
  bool saveTotals(float total, int32_t day) { return saveHydration(data.hasBaseline, data.baselineWeight, total, day); }
  bool saveBleMac(const uint8_t mac[6]) {
    return mac != nullptr && writePeer(true, mac, data.hasIrk, data.irk);
  }
  bool clearBleMac() {
    // A single tombstone snapshot atomically clears both legacy identity fields.
    // Legacy records remain migration inputs, but can never resurrect a peer
    // after a successful clear, including after reboot.
    const uint8_t empty[16] = {};
    return writePeer(false, empty, false, empty);
  }

  bool saveClassicPhone(const uint8_t mac[6], const char *name) {
    if (!mac || !name) return false;
    uint8_t any = 0, all = 255;
    for (size_t i = 0; i < 6; ++i) { any |= mac[i]; all &= mac[i]; }
    if (!any || all == 255) return false;
    char safe[49] = {};
    classicNameCopy(safe, reinterpret_cast<const uint8_t *>(name), strlen(name));
    return writeClassic(true, mac, safe);
  }
  bool clearClassicPhone() { return writeClassic(false, nullptr, ""); }

  bool saveDnd(bool on) { BottleNvmData d = data; d.dnd = on; return saveSettings(d); }
  bool saveColor(uint8_t r, uint8_t g, uint8_t b) {
    BottleNvmData d = data; d.colorR = r; d.colorG = g; d.colorB = b; return saveSettings(d);
  }
  bool saveTz(int16_t minutes) { BottleNvmData d = data; d.tzMin = minutes; return saveSettings(d); }
  bool saveWindow(uint16_t start, uint16_t end) {
    BottleNvmData d = data; d.startMin = start; d.endMin = end; return saveSettings(d);
  }
  bool saveInterval(uint16_t minutes) { BottleNvmData d = data; d.intervalMin = minutes; return saveSettings(d); }
  bool saveBeep(uint8_t type) { BottleNvmData d = data; d.beepType = type; return saveSettings(d); }

  bool saveNet(const char *ssid, const char *pass) {
    if (ssid == nullptr || ssid[0] == '\0' || !fits(ssid, 32) || !passOk(pass)) return false;
    BottleNvmData d = data;
    int found = -1;
    for (uint8_t i = 0; i < d.netCount; ++i) if (strcmp(d.nets[i].ssid, ssid) == 0) found = i;
    if (found < 0 && d.netCount == BOTTLE_MAX_NETWORKS) return false;  // Never evict.
    const uint8_t slot = found < 0 ? d.netCount++ : static_cast<uint8_t>(found);
    copyTo(d.nets[slot].ssid, sizeof(d.nets[slot].ssid), ssid);
    copyTo(d.nets[slot].pass, sizeof(d.nets[slot].pass), pass);  // "" explicitly means OPEN.
    return writeNets(d);
  }
  bool removeNet(const char *ssid) {
    if (ssid == nullptr || ssid[0] == '\0') return false;
    BottleNvmData d = data;
    uint8_t i = 0;
    while (i < d.netCount && strcmp(d.nets[i].ssid, ssid) != 0) ++i;
    if (i == d.netCount) return false;
    for (; i + 1 < d.netCount; ++i) d.nets[i] = d.nets[i + 1];
    memset(&d.nets[--d.netCount], 0, sizeof(BottleNetwork));
    return writeNets(d);
  }
  bool clearNets() {
    BottleNvmData d = data; d.netCount = 0; memset(d.nets, 0, sizeof(d.nets)); return writeNets(d);
  }

 private:
  Preferences prefs_;
  bool settingsBlob_ = false, hydrationBlob_ = false, networksBlob_ = false, peerBlob_ = false;
  bool classicBlob_ = false;
  static const size_t kClassicBytes = 4 + 1 + 1 + 6 + 49 + 4;
  static const uint32_t kClassicMagic = 0x31504342u;
  uint32_t legacyUsable_ = 0;

  static uint32_t classicCrc(const uint8_t *b, size_t n) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
      crc ^= b[i];
      for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
  }
  bool writeClassic(bool has, const uint8_t *mac, const char *name) {
    uint8_t b[kClassicBytes] = {};
    Writer w(b); w.u32(kClassicMagic); w.u8(1); w.u8(has ? 1 : 0);
    if (has) { memcpy(b + 6, mac, 6); memcpy(b + 12, name, strlen(name)); }
    Writer tail(b + kClassicBytes - 4); tail.u32(classicCrc(b, kClassicBytes - 4));
    if (prefs_.putBytes("classic_v1", b, sizeof(b)) != sizeof(b)) return false;
    data.hasClassicPhone = has;
    memcpy(data.classicMac, b + 6, 6); memcpy(data.classicName, b + 12, 49);
    classicBlob_ = true;
    return true;
  }
  void loadClassic() {
    // Deliberately no legacy fallback: only an explicitly selected atomic record.
    uint8_t b[kClassicBytes];
    if (prefs_.getBytesLength("classic_v1") != sizeof(b) ||
        prefs_.getBytes("classic_v1", b, sizeof(b)) != sizeof(b)) return;
    Reader r(b), tail(b + kClassicBytes - 4);
    if (r.u32() != kClassicMagic || r.u8() != 1 || b[5] > 1 ||
        tail.u32() != classicCrc(b, kClassicBytes - 4) || b[60] != 0) return;
    char safe[49] = {};
    classicNameCopy(safe, b + 12, 49);
    if (memcmp(safe, b + 12, 49) != 0) return;
    if (b[5]) {
      uint8_t any = 0, all = 255;
      for (int i = 6; i < 12; ++i) { any |= b[i]; all &= b[i]; }
      if (!any || all == 255) return;
      data.hasClassicPhone = true;
      memcpy(data.classicMac, b + 6, 6); memcpy(data.classicName, b + 12, 49);
    }
    classicBlob_ = true;
  }
  static const uint32_t kSettingsMagic = 0x32535442u;
  static const uint32_t kHydrationMagic = 0x32485942u;
  static const uint32_t kNetworksMagic = 0x324e5442u;
  static const size_t kSettingsBytes = 39;
  static const size_t kHydrationBytes = 21;
  static const uint32_t kPeerMagic = 0x32505442u;
  static const size_t kPeerBytes = 31;
  static const size_t kNetworkBytes = 4 + 1 + BOTTLE_MAX_NETWORKS * (BOTTLE_SSID_LEN + BOTTLE_PASS_LEN) + 4;

  struct Writer {
    uint8_t *p;
    explicit Writer(uint8_t *b) : p(b) {}
    void u8(uint8_t n) { *p++ = n; }
    void u16(uint16_t n) { u8(n & 255); u8(n >> 8); }
    void u32(uint32_t n) { u16(n & 65535); u16(n >> 16); }
    void f32(float f) { uint32_t u; memcpy(&u, &f, sizeof(u)); u32(u); }
  };
  struct Reader {
    const uint8_t *p;
    explicit Reader(const uint8_t *b) : p(b) {}
    uint8_t u8() { return *p++; }
    uint16_t u16() { const uint16_t lo = u8(); return lo | (static_cast<uint16_t>(u8()) << 8); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | (static_cast<uint32_t>(u16()) << 16); }
    float f32() { const uint32_t u = u32(); float f; memcpy(&f, &u, sizeof(f)); return f; }
  };
  static uint32_t checksum(const uint8_t *b, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
  }
  bool readBlob(const char *key, uint8_t *b, size_t n) {
    if (prefs_.getBytesLength(key) != n || prefs_.getBytes(key, b, n) != n) return false;
    Reader tail(b + n - 4);
    return tail.u32() == checksum(b, n - 4);
  }
  static bool validCal(float f) { return isfinite(f) && fabsf(f) >= 0.000001f; }
  static bool validBaseline(float f) { return isfinite(f) && f >= 100.0f; }
  static bool validTotal(float f) { return isfinite(f) && f >= 0; }
  static bool validDay(int32_t d) {
    if (d == 0) return true;
    if (d < 20230101 || d > 99991231) return false;
    const int year = d / 10000, month = (d / 100) % 100, day = d % 100;
    if (month < 1 || month > 12 || day < 1) return false;
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
  }

  static const char *settingName(int i) {
    static const char *const keys[] = {"dnd", "color", "start_min", "end_min", "interval_min", "beep_type", "tz_min",
      "missing_sec", "beep_delay_sec", "drink_grams", "settle_ms", "reminder_color", "missing_color", "presence_enabled"};
    return i >= 0 && i < 14 ? keys[i] : nullptr;
  }
  static int settingIndex(const char *key) {
    if (key == nullptr) return -1;
    for (int i = 0; i < 14; ++i) if (strcmp(key, settingName(i)) == 0) return i;
    return -1;
  }
  static bool isSetting(const char *key) { return settingIndex(key) >= 0; }
  static void copySettings(BottleNvmData &to, const BottleNvmData &d) {
    to.dnd = d.dnd; to.colorR = d.colorR; to.colorG = d.colorG; to.colorB = d.colorB;
    to.startMin = d.startMin; to.endMin = d.endMin; to.intervalMin = d.intervalMin;
    to.beepType = d.beepType; to.tzMin = d.tzMin; to.missingSec = d.missingSec;
    to.beepDelaySec = d.beepDelaySec; to.drinkGrams = d.drinkGrams; to.settleMs = d.settleMs;
    to.reminderColor = d.reminderColor; to.missingColor = d.missingColor; to.presenceEnabled = d.presenceEnabled;
  }
  void loadLegacySettings() {
    // Validate each legacy value independently; one corrupt key cannot wipe
    // otherwise usable settings. New blob snapshots validate as a whole.
    for (int i = 0; i < 14; ++i) {
      const char *key = settingName(i);
      if (!prefs_.isKey(key)) continue;
      BottleNvmData d = data;
      switch (i) {
        case 0: d.dnd = prefs_.getBool(key, false); break;
        case 1: {
          const uint32_t c = prefs_.getUInt(key, 0xffffffffu);
          if (c > 0xffffffu) continue;
          d.colorR = c >> 16; d.colorG = c >> 8; d.colorB = c; break;
        }
        case 2: d.startMin = prefs_.getUShort(key, 65535); break;
        case 3: d.endMin = prefs_.getUShort(key, 65535); break;
        case 4: d.intervalMin = prefs_.getUShort(key, 0); break;
        case 5: d.beepType = prefs_.getUChar(key, 255); break;
        case 6: d.tzMin = prefs_.getShort(key, 32767); break;
        case 7: d.missingSec = prefs_.getUShort(key, 0); break;
        case 8: d.beepDelaySec = prefs_.getUChar(key, 255); break;
        case 9: d.drinkGrams = prefs_.getUShort(key, 0); break;
        case 10: d.settleMs = prefs_.getUShort(key, 0); break;
        case 11: d.reminderColor = prefs_.getUInt(key, 0xffffffffu); break;
        case 12: d.missingColor = prefs_.getUInt(key, 0xffffffffu); break;
        case 13: d.presenceEnabled = true; break; // obsolete disabled setting cannot bypass policy
      }
      if (validSettings(d)) { copySettings(data, d); legacyUsable_ |= 1UL << i; }
    }
  }
  bool loadSettings() {
    uint8_t b[kSettingsBytes];
    if (!readBlob("settings_v2", b, sizeof(b))) return false;
    Reader r(b);
    if (r.u32() != kSettingsMagic || r.u16() != 2) return false;
    BottleNvmData d = data;
    const uint8_t dnd = r.u8(); d.dnd = dnd != 0;
    d.colorR = r.u8(); d.colorG = r.u8(); d.colorB = r.u8();
    d.startMin = r.u16(); d.endMin = r.u16(); d.intervalMin = r.u16();
    d.beepType = r.u8(); d.tzMin = static_cast<int16_t>(r.u16());
    d.missingSec = r.u16(); d.beepDelaySec = r.u8(); d.drinkGrams = r.u16(); d.settleMs = r.u16();
    d.reminderColor = r.u32(); d.missingColor = r.u32();
    const uint8_t presence = r.u8(); d.presenceEnabled = true; // normalize legacy false safely
    if (dnd > 1 || presence > 1 || !validSettings(d)) return false;
    copySettings(data, d); settingsBlob_ = true;
    if (presence == 0) saveSettings(data); // best effort migration; runtime remains mandatory on write failure
    return true;
  }
  void loadHydration() {
    uint8_t b[kHydrationBytes];
    if (!readBlob("hydration_v2", b, sizeof(b))) return;
    Reader r(b);
    if (r.u32() != kHydrationMagic) return;
    const uint8_t has = r.u8(); const float baseline = r.f32(), total = r.f32();
    const int32_t day = static_cast<int32_t>(r.u32());
    if (has > 1 || (has && !validBaseline(baseline)) || !isfinite(baseline) || !validTotal(total) || !validDay(day)) return;
    data.hasBaseline = has != 0; data.baselineWeight = has ? baseline : 0;
    data.dailyTotalMl = total; data.lastDay = day; hydrationBlob_ = true;
  }
  bool writePeer(bool hasMac, const uint8_t *mac, bool hasIrk, const uint8_t *irk) {
    uint8_t b[kPeerBytes] = {};
    Writer w(b); w.u32(kPeerMagic); w.u8((hasMac ? 1 : 0) | (hasIrk ? 2 : 0));
    if (hasMac) memcpy(w.p, mac, 6);
    w.p += 6;
    if (hasIrk) memcpy(w.p, irk, 16);
    w.p += 16;
    w.u32(checksum(b, kPeerBytes - 4));
    if (prefs_.putBytes("peer_v2", b, sizeof(b)) != sizeof(b)) return false;
    data.hasBleMac = hasMac; data.hasIrk = hasIrk;
    memcpy(data.bleMac, b + 5, 6); memcpy(data.irk, b + 11, 16); peerBlob_ = true;
    return true;
  }
  void loadPeer() {
    uint8_t b[kPeerBytes];
    if (!readBlob("peer_v2", b, sizeof(b))) return;
    Reader r(b);
    if (r.u32() != kPeerMagic) return;
    const uint8_t flags = r.u8();
    if (flags > 3) return;
    data.hasBleMac = (flags & 1) != 0; data.hasIrk = (flags & 2) != 0;
    memcpy(data.bleMac, b + 5, 6); memcpy(data.irk, b + 11, 16);
    if (!data.hasBleMac) memset(data.bleMac, 0, sizeof(data.bleMac));
    if (!data.hasIrk) memset(data.irk, 0, sizeof(data.irk));
    peerBlob_ = true;
  }
  void loadNets() {
    uint8_t b[kNetworkBytes];
    if (readBlob("networks_v2", b, sizeof(b))) {
      Reader r(b);
      if (r.u32() == kNetworksMagic) {
        const uint8_t count = r.u8();
        BottleNetwork nets[BOTTLE_MAX_NETWORKS] = {};
        memcpy(nets, r.p, sizeof(nets));
        bool ok = count <= BOTTLE_MAX_NETWORKS;
        for (uint8_t i = 0; ok && i < count; ++i) {
          ok = memchr(nets[i].ssid, 0, sizeof(nets[i].ssid)) && memchr(nets[i].pass, 0, sizeof(nets[i].pass)) &&
               nets[i].ssid[0] != '\0' && fits(nets[i].ssid, 32) && passOk(nets[i].pass);
          for (uint8_t j = 0; ok && j < i; ++j) ok = strcmp(nets[i].ssid, nets[j].ssid) != 0;
        }
        if (ok) {
          data.netCount = count; memcpy(data.nets, nets, sizeof(nets)); networksBlob_ = true;
          syncPrimary(data); return;
        }
      }
    }
    data.netCount = 0; memset(data.nets, 0, sizeof(data.nets));
    for (uint8_t i = 0; i < BOTTLE_MAX_NETWORKS; ++i) {
      char sk[12], pk[12], ssid[BOTTLE_SSID_LEN] = {}, pass[BOTTLE_PASS_LEN] = {};
      snprintf(sk, sizeof(sk), "wifi_s%u", i); snprintf(pk, sizeof(pk), "wifi_p%u", i);
      loadString(sk, ssid, sizeof(ssid)); loadString(pk, pass, sizeof(pass));
      bool duplicate = false;
      for (uint8_t j = 0; j < data.netCount; ++j) if (strcmp(data.nets[j].ssid, ssid) == 0) duplicate = true;
      if (!duplicate && ssid[0] && fits(ssid, 32) && passOk(pass)) {
        copyTo(data.nets[data.netCount].ssid, BOTTLE_SSID_LEN, ssid);
        copyTo(data.nets[data.netCount++].pass, BOTTLE_PASS_LEN, pass);
      }
    }
    if (data.netCount == 0 && data.staSsid[0] && fits(data.staSsid, 32) && passOk(data.staPass)) {
      copyTo(data.nets[0].ssid, BOTTLE_SSID_LEN, data.staSsid);
      copyTo(data.nets[0].pass, BOTTLE_PASS_LEN, data.staPass); data.netCount = 1;
    }
    syncPrimary(data);
  }
  bool writeNets(BottleNvmData &d) {
    uint8_t b[kNetworkBytes] = {};
    Writer w(b); w.u32(kNetworksMagic); w.u8(d.netCount);
    memcpy(w.p, d.nets, sizeof(d.nets)); w.p += sizeof(d.nets);
    w.u32(checksum(b, kNetworkBytes - 4));
    if (prefs_.putBytes("networks_v2", b, sizeof(b)) != sizeof(b)) return false;
    data.netCount = d.netCount; memcpy(data.nets, d.nets, sizeof(data.nets));
    syncPrimary(data); networksBlob_ = true;
    return true;
  }
  static void syncPrimary(BottleNvmData &d) {
    copyTo(d.staSsid, sizeof(d.staSsid), d.netCount ? d.nets[0].ssid : "");
    copyTo(d.staPass, sizeof(d.staPass), d.netCount ? d.nets[0].pass : "");
  }
  void loadString(const char *key, char *dest, size_t len) {
    dest[0] = '\0';
    if (!prefs_.isKey(key)) return;
    // Do not truncate an overlong stored credential into a different network.
    if (prefs_.getString(key, dest, len) == 0) dest[0] = '\0';
    dest[len - 1] = '\0';
  }
  static bool fits(const char *s, size_t n) { return s != nullptr && strlen(s) <= n; }
  static bool passOk(const char *s) { return s != nullptr && (s[0] == '\0' || (strlen(s) >= 8 && strlen(s) <= 63)); }
  static void copyTo(char *dest, size_t n, const char *s) {
    strncpy(dest, s ? s : "", n - 1); dest[n - 1] = '\0';
  }
};
