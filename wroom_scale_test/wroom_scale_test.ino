// ESP32-WROOM bring-up: scale + white + RGB + buzzer.
// Scale: DT=32 SCK=33 VCC=3V3 GND=GND
// White: GPIO25 -- resistor -- LED+ ; LED- -- GND
// RGB common-anode: anode -- 3V3 ; R=27 G=14 B=12 (with resistors)
// Buzzer: GPIO26 -- + ; - -- GND
// Serial 115200.
//
// t / c <g> / w     scale
// l <0-255>         white LED brightness
// p <r> <g> <b>     RGB brightness 0-255 each (common-anode)
// p blue            blue only, full
// p red / p green   that channel full
// b 1 / b 0         buzzer
// o                 all outputs off

#include <Preferences.h>

#include "src/HX711.h"

static const int PIN_DT = 32;
static const int PIN_SCK = 33;
static const int PIN_WHITE = 25;
static const int PIN_BUZZER = 26;
static const int PIN_R = 27;
static const int PIN_G = 14;
static const int PIN_B = 12;
static const char *NVS_NS = "wroom_scale";
static const long RAIL = 8388000L;

HX711 scale;
Preferences prefs;
bool hasTare = false;
bool hasCal = false;
long tareOffset = 0;
float calFactor = 0.0f;
uint32_t lastPrintMs = 0;
uint8_t whiteLevel_ = 0;
uint8_t levelR_ = 0;
uint8_t levelG_ = 0;
uint8_t levelB_ = 0;
bool buzzerOn_ = false;
char line[80];
uint8_t lineLen = 0;

bool goodRaw(long raw) {
  if (raw == 0L) {
    return false;
  }
  if (raw <= -RAIL || raw >= RAIL) {
    return false;
  }
  return true;
}

void setWhite(uint8_t level) {
  whiteLevel_ = level;
  analogWrite(PIN_WHITE, level);
}

// Common-anode: channel lights when the pin is pulled low. 255 = brightest.
void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  levelR_ = r;
  levelG_ = g;
  levelB_ = b;
  analogWrite(PIN_R, 255 - r);
  analogWrite(PIN_G, 255 - g);
  analogWrite(PIN_B, 255 - b);
}

void setBuzzer(bool on) {
  buzzerOn_ = on;
  // Active buzzer (old bottle): DC HIGH on GPIO26, other lead to GND.
  // Passive buzzers need a square wave — use command: b tone
  noTone(PIN_BUZZER);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

void setBuzzerTone(bool on) {
  buzzerOn_ = on;
  if (on) {
    // Passive buzzer: ~2 kHz on GPIO26.
    tone(PIN_BUZZER, 2000);
  } else {
    noTone(PIN_BUZZER);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

void loadNvm() {
  prefs.begin(NVS_NS, false);
  hasTare = prefs.isKey("tare_offset");
  tareOffset = hasTare ? prefs.getLong("tare_offset", 0) : 0;
  hasCal = prefs.isKey("cal_factor");
  calFactor = hasCal ? prefs.getFloat("cal_factor", 0.0f) : 0.0f;
  if (calFactor == 0.0f) {
    hasCal = false;
  }
  if (hasTare && !goodRaw(tareOffset)) {
    prefs.remove("tare_offset");
    hasTare = false;
    tareOffset = 0;
    Serial.println("cleared bad saved zero");
  }
  if (hasTare) {
    scale.set_offset(tareOffset);
  }
  if (hasCal) {
    scale.set_scale(calFactor);
  }
}

bool saveTare(long offset) {
  tareOffset = offset;
  hasTare = true;
  scale.set_offset(offset);
  return prefs.putLong("tare_offset", offset) > 0;
}

bool saveCal(float factor) {
  calFactor = factor;
  hasCal = factor != 0.0f;
  if (hasCal) {
    scale.set_scale(factor);
  }
  return prefs.putFloat("cal_factor", factor) > 0;
}

bool averageGood(long *out, byte times) {
  long sum = 0;
  int count = 0;
  for (byte i = 0; i < times; i++) {
    if (!scale.wait_ready_retry(20, 10)) {
      continue;
    }
    const long raw = scale.read();
    if (!goodRaw(raw)) {
      continue;
    }
    sum += raw;
    count++;
  }
  if (count < (times + 1) / 2) {
    return false;
  }
  *out = sum / count;
  return true;
}

uint8_t clamp255(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 255) {
    return 255;
  }
  return (uint8_t)v;
}

void printRgb() {
  Serial.print("rgb ");
  Serial.print(levelR_);
  Serial.print(' ');
  Serial.print(levelG_);
  Serial.print(' ');
  Serial.println(levelB_);
}

void printHelp() {
  Serial.println("t zero | c <g> cal | w weight");
  Serial.println("l <0-255> white | p <r> <g> <b> RGB | p blue");
  Serial.println("b 1 = buzzer HIGH on GPIO26 (meter ~3.3V to GND)");
  Serial.println("b 0 = off | b tone = passive buzzer beep | o all off");
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
    long offset = 0;
    if (!averageGood(&offset, 10)) {
      Serial.println("tare failed: bad HX711 link");
    } else if (saveTare(offset)) {
      Serial.print("zero saved ");
      Serial.println(offset);
    } else {
      Serial.println("tare save failed");
    }
  } else if (cmd == 'c' || cmd == 'C') {
    const float known = strtof(line + 1, nullptr);
    long raw = 0;
    if (!hasTare || known <= 0.0f) {
      Serial.println("calibrate failed. Zero first, then c <grams>.");
    } else if (!averageGood(&raw, 10)) {
      Serial.println("calibrate failed: bad HX711 link");
    } else {
      const float factor = ((float)raw - (float)tareOffset) / known;
      if (factor == 0.0f || !saveCal(factor)) {
        Serial.println("calibrate failed");
      } else {
        Serial.print("cal_factor ");
        Serial.println(factor, 4);
      }
    }
  } else if (cmd == 'w' || cmd == 'W') {
    lastPrintMs = 0;
  } else if (cmd == 'l' || cmd == 'L') {
    char *arg = line + 1;
    while (*arg == ' ') {
      arg++;
    }
    if (*arg == '\0') {
      setWhite(whiteLevel_ ? 0 : 255);
    } else {
      setWhite(clamp255(atoi(arg)));
    }
    Serial.print("led ");
    Serial.println(whiteLevel_);
  } else if (cmd == 'p' || cmd == 'P') {
    char *arg = line + 1;
    while (*arg == ' ') {
      arg++;
    }
    if (*arg == '\0') {
      printRgb();
    } else if (strncmp(arg, "blue", 4) == 0 || strncmp(arg, "BLUE", 4) == 0) {
      setRgb(0, 0, 255);
      printRgb();
    } else if (strncmp(arg, "red", 3) == 0 || strncmp(arg, "RED", 3) == 0) {
      setRgb(255, 0, 0);
      printRgb();
    } else if (strncmp(arg, "green", 5) == 0 || strncmp(arg, "GREEN", 5) == 0) {
      setRgb(0, 255, 0);
      printRgb();
    } else if (strncmp(arg, "white", 5) == 0 || strncmp(arg, "WHITE", 5) == 0) {
      setRgb(255, 255, 255);
      printRgb();
    } else {
      int r = 0, g = 0, b = 0;
      if (sscanf(arg, "%d %d %d", &r, &g, &b) == 3) {
        setRgb(clamp255(r), clamp255(g), clamp255(b));
        printRgb();
      } else {
        Serial.println("use: p <r> <g> <b>   or p blue");
      }
    }
  } else if (cmd == 'b' || cmd == 'B') {
    char *arg = line + 1;
    while (*arg == ' ') {
      arg++;
    }
    if (*arg == '\0') {
      setBuzzer(!buzzerOn_);
      Serial.println(buzzerOn_ ? "buzzer on GPIO26 HIGH" : "buzzer off");
    } else if (strncmp(arg, "tone", 4) == 0 || strncmp(arg, "TONE", 4) == 0) {
      setBuzzerTone(true);
      Serial.println("buzzer tone 2kHz on GPIO26 (passive). b 0 to stop");
    } else {
      setBuzzer(atoi(arg) != 0);
      Serial.println(buzzerOn_ ? "buzzer on GPIO26 HIGH (~3.3V)" : "buzzer off");
    }
  } else if (cmd == 'o' || cmd == 'O') {
    setWhite(0);
    setRgb(0, 0, 0);
    setBuzzer(false);
    Serial.println("outputs off");
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

void printSample(uint32_t now) {
  if (now - lastPrintMs < 1000) {
    return;
  }
  lastPrintMs = now;
  if (!scale.is_ready()) {
    Serial.println("scale: not ready");
    return;
  }
  const long raw = scale.read();
  Serial.print("raw ");
  Serial.print(raw);
  if (!goodRaw(raw)) {
    Serial.println("  BAD link");
    return;
  }
  if (hasTare && hasCal) {
    const float grams = (float)((double)raw - (double)tareOffset) / calFactor;
    Serial.print("  g ");
    Serial.println(grams, 1);
  } else {
    Serial.println("  (t then c <grams>)");
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  pinMode(PIN_WHITE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  setWhite(0);
  setRgb(0, 0, 0);
  setBuzzer(false);

  scale.begin(PIN_DT, PIN_SCK);
  scale.power_down();
  delay(1);
  scale.power_up();
  delay(400);
  loadNvm();

  Serial.println();
  Serial.println("wroom bring-up (scale + rgb + white + buzzer)");
  Serial.println(hasTare ? "zero: ok" : "zero: not zeroed");
  Serial.println(hasCal ? "cal: ok" : "cal: not calibrated");
  printHelp();

  // Show blue at full software brightness so you can compare to the old board.
  setRgb(0, 0, 255);
  Serial.println("blue full (p blue). Try p 0 0 255 or p 0 0 200");
  Serial.println("buzzer test: GPIO26 HIGH for 1s — measure 26 to GND");
  setBuzzer(true);
  delay(1000);
  setBuzzer(false);
  Serial.println("buzzer test done. Type: b 1");
}

void loop() {
  pollSerial();
  printSample(millis());
}
