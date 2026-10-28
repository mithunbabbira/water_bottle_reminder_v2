// ESP32-C3 Super Mini + HX711.
// DT -> GPIO4, SCK -> GPIO3, VCC -> 3V3, GND -> GND.
// The empty-scale raw count is stored in NVM (namespace "scale", key "tare_offset").
// Serial 115200: t + Enter stores a new zero from the current reading.

#include <Preferences.h>

static const int PIN_DT = 4;
static const int PIN_SCK = 3;
static const int TARE_SAMPLES = 10;
static const long READ_ERROR = 0x80000000L;
static const char *NVS_NS = "scale";
static const char *NVS_KEY = "tare_offset";

Preferences prefs;
long tareOffset = 0;
bool tareValid = false;

void hx711PowerUp() {
  digitalWrite(PIN_SCK, HIGH);
  delayMicroseconds(80);
  digitalWrite(PIN_SCK, LOW);
  delay(1);
}

long hx711ReadRaw() {
  const uint32_t start = millis();
  while (digitalRead(PIN_DT) == HIGH) {
    if (millis() - start > 1000) {
      return READ_ERROR;
    }
  }

  uint32_t value = 0;
  for (int i = 0; i < 24; i++) {
    digitalWrite(PIN_SCK, HIGH);
    delayMicroseconds(1);
    value = (value << 1) | (uint32_t)digitalRead(PIN_DT);
    digitalWrite(PIN_SCK, LOW);
    delayMicroseconds(1);
  }

  digitalWrite(PIN_SCK, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_SCK, LOW);

  if (value & 0x800000UL) {
    value |= 0xFF000000UL;
  }
  return (long)value;
}

bool saveZero(long offset) {
  tareOffset = offset;
  tareValid = true;
  return prefs.putLong(NVS_KEY, (int32_t)offset) > 0;
}

bool captureZero() {
  long sum = 0;
  int count = 0;
  for (int i = 0; i < TARE_SAMPLES; i++) {
    const long raw = hx711ReadRaw();
    if (raw == READ_ERROR) {
      continue;
    }
    sum += raw;
    count++;
  }
  if (count == 0) {
    return false;
  }
  return saveZero(sum / count);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_DT, INPUT);
  digitalWrite(PIN_SCK, LOW);
  delay(300);

  Serial.println();
  Serial.println("STEP 1  board is up and USB serial works");

  hx711PowerUp();
  hx711ReadRaw();

  prefs.begin(NVS_NS, false);
  if (prefs.isKey(NVS_KEY)) {
    tareOffset = prefs.getLong(NVS_KEY, 0);
    tareValid = true;
    Serial.print("STEP 2  loaded zero from NVM: ");
    Serial.println(tareOffset);
  } else if (captureZero()) {
    Serial.print("STEP 2  no saved zero, stored empty-scale zero: ");
    Serial.println(tareOffset);
  } else {
    Serial.println("STEP 2  could not read a zero. Check DT/SCK/3V3.");
  }
  Serial.println("        t + Enter stores a new zero. Leave the scale empty.");
}

void loop() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c != 't' && c != 'T') {
      continue;
    }
    Serial.println("taring...");
    if (captureZero()) {
      Serial.print("zero saved to NVM: ");
      Serial.println(tareOffset);
    } else {
      Serial.println("tare failed, NVM unchanged");
    }
  }

  const long raw = hx711ReadRaw();
  if (raw == READ_ERROR) {
    Serial.println("no data. DT stayed HIGH.");
    delay(250);
    return;
  }

  Serial.print("raw = ");
  Serial.print(raw);
  if (tareValid) {
    Serial.print("  zeroed = ");
    Serial.println(raw - tareOffset);
  } else {
    Serial.println("  zeroed = (none)");
  }
  delay(250);
}
