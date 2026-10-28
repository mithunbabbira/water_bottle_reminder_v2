// Flash values for the bottle. Namespace "bottle".
// grams = (raw - tare_offset) / cal_factor
//
// Wi-Fi matches WLED's access-point behavior (wled00/const.h):
//   0  open the setup AP if the saved network is not joined after boot
//   1  open it whenever that connection drops
//   2  keep the AP on even while joined
//   3  open the AP only after a long button press
//   4  same as 0, then close the AP after five minutes
// No saved network means the setup AP is the only way in.

#pragma once

#include <Arduino.h>
#include <Preferences.h>

static const char *BOTTLE_NVS_NS = "bottle";

static const size_t BOTTLE_SSID_LEN = 33;
static const size_t BOTTLE_PASS_LEN = 65;
static const uint8_t BOTTLE_MAX_NETWORKS = 4;

static const uint8_t BOTTLE_AP_BOOT_NO_CONN = 0;
static const uint8_t BOTTLE_AP_NO_CONN = 1;
static const uint8_t BOTTLE_AP_ALWAYS = 2;
static const uint8_t BOTTLE_AP_BUTTON_ONLY = 3;
static const uint8_t BOTTLE_AP_TEMPORARY = 4;

static const uint8_t BEEP_OFF = 0;
static const uint8_t BEEP_SINGLE = 1;
static const uint8_t BEEP_DOUBLE = 2;
static const uint8_t BEEP_LONG = 3;

struct BottleNetwork {
  char ssid[BOTTLE_SSID_LEN];
  char pass[BOTTLE_PASS_LEN];
};

struct BottleNvmData {
  bool hasTare;
  int32_t tareOffset;

  bool hasCal;
  float calFactor;

  bool hasBaseline;
  float baselineWeight;

  float dailyTotalMl;
  int32_t lastDay;

  bool hasBleMac;
  uint8_t bleMac[6];

  bool hasIrk;
  uint8_t irk[16];

  bool dnd;
  uint8_t colorR;
  uint8_t colorG;
  uint8_t colorB;
  uint16_t startMin;
  uint16_t endMin;
  uint16_t intervalMin;
  uint8_t beepType;

  char staSsid[BOTTLE_SSID_LEN];
  char staPass[BOTTLE_PASS_LEN];
  uint8_t netCount;
  BottleNetwork nets[BOTTLE_MAX_NETWORKS];
  char apSsid[BOTTLE_SSID_LEN];
  char apPass[BOTTLE_PASS_LEN];
  uint8_t apBehavior;
};

class BottleNvm {
 public:
  BottleNvmData data;

  bool begin() { return prefs_.begin(BOTTLE_NVS_NS, false); }

  bool stored(const char *key) { return prefs_.isKey(key); }

  void load() {
    data.hasTare = prefs_.isKey("tare_offset");
    data.tareOffset = data.hasTare ? prefs_.getLong("tare_offset", 0) : 0;

    data.hasCal = prefs_.isKey("cal_factor");
    data.calFactor = data.hasCal ? prefs_.getFloat("cal_factor", 0.0f) : 0.0f;
    if (data.calFactor == 0.0f) {
      data.hasCal = false;
    }

    data.hasBaseline = prefs_.isKey("baseline_weight");
    data.baselineWeight = data.hasBaseline ? prefs_.getFloat("baseline_weight", 0.0f) : 0.0f;

    data.dailyTotalMl = prefs_.getFloat("daily_total", 0.0f);
    data.lastDay = prefs_.getInt("last_day", 0);

    data.hasBleMac = prefs_.getBytes("ble_mac", data.bleMac, sizeof(data.bleMac)) == 6;
    if (!data.hasBleMac) {
      memset(data.bleMac, 0, sizeof(data.bleMac));
    }

    data.hasIrk = prefs_.getBytes("ble_irk", data.irk, sizeof(data.irk)) == sizeof(data.irk);
    if (!data.hasIrk) {
      memset(data.irk, 0, sizeof(data.irk));
    }

    data.dnd = prefs_.getBool("dnd", false);

    const uint32_t color = prefs_.getUInt("color", 0x8000FFu);
    data.colorR = (color >> 16) & 0xFF;
    data.colorG = (color >> 8) & 0xFF;
    data.colorB = color & 0xFF;

    data.startMin = prefs_.getUShort("start_min", 23 * 60);
    data.endMin = prefs_.getUShort("end_min", 10 * 60);
    data.intervalMin = prefs_.getUShort("interval_min", 30);
    data.beepType = prefs_.getUChar("beep_type", BEEP_DOUBLE);

    loadString("sta_ssid", data.staSsid, sizeof(data.staSsid), "");
    loadString("sta_pass", data.staPass, sizeof(data.staPass), "");
    loadNets();
    loadString("ap_ssid", data.apSsid, sizeof(data.apSsid), "bottle-AP");
    loadString("ap_pass", data.apPass, sizeof(data.apPass), "");
    data.apBehavior = prefs_.getUChar("ap_behavior", BOTTLE_AP_BOOT_NO_CONN);
  }

  bool saveTare(int32_t offset) {
    data.tareOffset = offset;
    data.hasTare = true;
    return prefs_.putLong("tare_offset", offset) > 0;
  }

  bool saveCal(float factor) {
    data.calFactor = factor;
    data.hasCal = factor != 0.0f;
    return prefs_.putFloat("cal_factor", factor) > 0;
  }

  bool saveBaseline(float grams) {
    data.baselineWeight = grams;
    data.hasBaseline = true;
    return prefs_.putFloat("baseline_weight", grams) > 0;
  }

  void clearBaseline() {
    prefs_.remove("baseline_weight");
    data.baselineWeight = 0.0f;
    data.hasBaseline = false;
  }

  bool saveTotals(float dailyTotalMl, int32_t day) {
    data.dailyTotalMl = dailyTotalMl;
    data.lastDay = day;
    return prefs_.putFloat("daily_total", dailyTotalMl) > 0 && prefs_.putInt("last_day", day) > 0;
  }

  bool saveBleMac(const uint8_t mac[6]) {
    memcpy(data.bleMac, mac, 6);
    data.hasBleMac = true;
    return prefs_.putBytes("ble_mac", mac, 6) == 6;
  }

  bool saveIrk(const uint8_t key[16]) {
    memcpy(data.irk, key, sizeof(data.irk));
    data.hasIrk = true;
    return prefs_.putBytes("ble_irk", key, sizeof(data.irk)) == sizeof(data.irk);
  }

  void clearBleMac() {
    prefs_.remove("ble_mac");
    prefs_.remove("ble_irk");
    memset(data.bleMac, 0, sizeof(data.bleMac));
    memset(data.irk, 0, sizeof(data.irk));
    data.hasBleMac = false;
    data.hasIrk = false;
  }

  bool saveDnd(bool on) {
    data.dnd = on;
    return prefs_.putBool("dnd", on) > 0;
  }

  bool saveColor(uint8_t r, uint8_t g, uint8_t b) {
    data.colorR = r;
    data.colorG = g;
    data.colorB = b;
    const uint32_t packed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    return prefs_.putUInt("color", packed) > 0;
  }

  bool saveWindow(uint16_t startMin, uint16_t endMin) {
    if (startMin >= 24 * 60 || endMin >= 24 * 60) {
      return false;
    }
    data.startMin = startMin;
    data.endMin = endMin;
    return prefs_.putUShort("start_min", startMin) > 0 && prefs_.putUShort("end_min", endMin) > 0;
  }

  bool saveInterval(uint16_t minutes) {
    if (minutes == 0) {
      return false;
    }
    data.intervalMin = minutes;
    return prefs_.putUShort("interval_min", minutes) > 0;
  }

  bool saveBeep(uint8_t type) {
    if (type > BEEP_LONG) {
      return false;
    }
    data.beepType = type;
    return prefs_.putUChar("beep_type", type) > 0;
  }

  bool saveSta(const char *ssid, const char *pass) {
    if (ssid == nullptr || pass == nullptr) {
      return false;
    }
    if (ssid[0] == '\0') {
      return clearNets();
    }
    return saveNet(ssid, pass);
  }

  bool saveNet(const char *ssid, const char *pass) {
    if (ssid == nullptr || ssid[0] == '\0' || !fits(ssid, 32)) {
      return false;
    }
    int found = -1;
    for (uint8_t i = 0; i < data.netCount; ++i) {
      if (strcmp(data.nets[i].ssid, ssid) == 0) {
        found = i;
        break;
      }
    }
    const char *finalPass = pass != nullptr ? pass : "";
    if (finalPass[0] == '\0' && found >= 0) {
      finalPass = data.nets[found].pass;
    }
    if (!passOk(finalPass)) {
      return false;
    }
    char savedPass[BOTTLE_PASS_LEN];
    copyTo(savedPass, sizeof(savedPass), finalPass);

    if (found >= 0) {
      for (int i = found; i > 0; --i) {
        data.nets[i] = data.nets[i - 1];
      }
    } else {
      const uint8_t nextCount = data.netCount < BOTTLE_MAX_NETWORKS ? (data.netCount + 1) : BOTTLE_MAX_NETWORKS;
      for (int i = (int)nextCount - 1; i > 0; --i) {
        data.nets[i] = data.nets[i - 1];
      }
      data.netCount = nextCount;
    }
    copyTo(data.nets[0].ssid, sizeof(data.nets[0].ssid), ssid);
    copyTo(data.nets[0].pass, sizeof(data.nets[0].pass), savedPass);
    return writeNets();
  }

  bool removeNet(const char *ssid) {
    if (ssid == nullptr || ssid[0] == '\0') {
      return false;
    }
    int found = -1;
    for (uint8_t i = 0; i < data.netCount; ++i) {
      if (strcmp(data.nets[i].ssid, ssid) == 0) {
        found = i;
        break;
      }
    }
    if (found < 0) {
      return false;
    }
    for (uint8_t i = (uint8_t)found; i + 1 < data.netCount; ++i) {
      data.nets[i] = data.nets[i + 1];
    }
    memset(&data.nets[data.netCount - 1], 0, sizeof(BottleNetwork));
    data.netCount--;
    return writeNets();
  }

  bool clearNets() {
    data.netCount = 0;
    memset(data.nets, 0, sizeof(data.nets));
    return writeNets();
  }

  bool saveAp(const char *ssid, const char *pass, uint8_t behavior) {
    if (ssid == nullptr || ssid[0] == '\0' || !fits(ssid, 32) || !passOk(pass)) {
      return false;
    }
    if (behavior > BOTTLE_AP_TEMPORARY) {
      return false;
    }
    if (pass != nullptr && pass[0] != '\0' && strlen(pass) < 8) {
      return false;
    }
    copyTo(data.apSsid, sizeof(data.apSsid), ssid);
    copyTo(data.apPass, sizeof(data.apPass), pass);
    data.apBehavior = behavior;
    return saveString("ap_ssid", data.apSsid) && saveString("ap_pass", data.apPass) &&
           prefs_.putUChar("ap_behavior", behavior) > 0;
  }

 private:
  Preferences prefs_;

  void loadNets() {
    data.netCount = 0;
    memset(data.nets, 0, sizeof(data.nets));
    for (uint8_t i = 0; i < BOTTLE_MAX_NETWORKS; ++i) {
      char sk[8], pk[8], ssid[BOTTLE_SSID_LEN], pass[BOTTLE_PASS_LEN];
      snprintf(sk, sizeof(sk), "wifi_s%u", i);
      snprintf(pk, sizeof(pk), "wifi_p%u", i);
      loadString(sk, ssid, sizeof(ssid), "");
      loadString(pk, pass, sizeof(pass), "");
      if (ssid[0] != '\0' && fits(ssid, 32) && passOk(pass)) {
        copyTo(data.nets[data.netCount].ssid, sizeof(data.nets[data.netCount].ssid), ssid);
        copyTo(data.nets[data.netCount].pass, sizeof(data.nets[data.netCount].pass), pass);
        data.netCount++;
      }
    }
    if (data.netCount == 0 && data.staSsid[0] != '\0' && fits(data.staSsid, 32) && passOk(data.staPass)) {
      copyTo(data.nets[0].ssid, sizeof(data.nets[0].ssid), data.staSsid);
      copyTo(data.nets[0].pass, sizeof(data.nets[0].pass), data.staPass);
      data.netCount = 1;
    }
    syncPrimarySta();
  }

  bool writeNets() {
    syncPrimarySta();
    bool ok = saveString("sta_ssid", data.staSsid) && saveString("sta_pass", data.staPass);
    for (uint8_t i = 0; i < BOTTLE_MAX_NETWORKS; ++i) {
      char sk[8], pk[8];
      snprintf(sk, sizeof(sk), "wifi_s%u", i);
      snprintf(pk, sizeof(pk), "wifi_p%u", i);
      const char *s = i < data.netCount ? data.nets[i].ssid : "";
      const char *p = i < data.netCount ? data.nets[i].pass : "";
      ok = saveString(sk, s) && saveString(pk, p) && ok;
    }
    return ok;
  }

  void syncPrimarySta() {
    if (data.netCount > 0) {
      copyTo(data.staSsid, sizeof(data.staSsid), data.nets[0].ssid);
      copyTo(data.staPass, sizeof(data.staPass), data.nets[0].pass);
    } else {
      data.staSsid[0] = '\0';
      data.staPass[0] = '\0';
    }
  }

  void loadString(const char *key, char *dest, size_t destLen, const char *fallback) {
    if (!prefs_.isKey(key)) {
      copyTo(dest, destLen, fallback);
      return;
    }
    prefs_.getString(key, dest, destLen);
  }

  bool saveString(const char *key, const char *value) {
    if (value == nullptr || value[0] == '\0') {
      prefs_.remove(key);
      return true;
    }
    return prefs_.putString(key, value) > 0;
  }

  static bool fits(const char *value, size_t maxLen) {
    return value != nullptr && strlen(value) <= maxLen;
  }

  static bool passOk(const char *pass) {
    if (pass == nullptr) {
      return false;
    }
    const size_t n = strlen(pass);
    return n == 0 || (n >= 8 && n <= 63);
  }

  static void copyTo(char *dest, size_t destLen, const char *value) {
    if (value == nullptr) {
      dest[0] = '\0';
      return;
    }
    strncpy(dest, value, destLen - 1);
    dest[destLen - 1] = '\0';
  }
};
