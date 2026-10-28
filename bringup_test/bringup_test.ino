// Bring-up for the ESP32-C3 Super Mini.
// HX711 DT GPIO4, SCK GPIO3, VCC 3V3, GND GND.
// White LED + GPIO1, - GND. Buzzer + GPIO10, - GND.
// RGB common anode on 3V3. Color legs: GPIO5, GPIO6, GPIO7. Pin LOW = lit.
// Serial 115200. Opening the monitor resets the board and replays the test.
// Keys: a = replay outputs, w = white, z = buzzer, 5/6/7 = one color, o = outputs off, t = save empty-scale zero.

#include <Preferences.h>

static const int PIN_DT = 4;
static const int PIN_SCK = 3;
static const int PIN_WHITE = 1;
static const int PIN_BUZZER = 10;
static const int PIN_R = 5;
static const int PIN_G = 6;
static const int PIN_B = 7;

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

void outputsOff() {
  digitalWrite(PIN_WHITE, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_R, HIGH);
  digitalWrite(PIN_G, HIGH);
  digitalWrite(PIN_B, HIGH);
}

void showColor(int pin, const char *name) {
  outputsOff();
  digitalWrite(pin, LOW);
  Serial.print("color ");
  Serial.print(name);
  Serial.print("  GPIO");
  Serial.print(pin);
  Serial.println(" is LOW");
  delay(800);
  outputsOff();
}

void playOutputs() {
  Serial.println("1  white LED on");
  outputsOff();
  digitalWrite(PIN_WHITE, HIGH);
  delay(800);
  outputsOff();

  Serial.println("2  buzzer");
  digitalWrite(PIN_BUZZER, HIGH);
  delay(200);
  digitalWrite(PIN_BUZZER, LOW);
  delay(200);

  showColor(PIN_R, "red");
  showColor(PIN_G, "green");
  showColor(PIN_B, "blue");
  Serial.println("outputs off. weight follows.");
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

void handleKey(char c) {
  if (c == 'a' || c == 'A') {
    playOutputs();
    return;
  }
  if (c == 'w' || c == 'W') {
    const bool on = digitalRead(PIN_WHITE) == LOW;
    digitalWrite(PIN_WHITE, on ? HIGH : LOW);
    Serial.println(on ? "white on" : "white off");
    return;
  }
  if (c == 'z' || c == 'Z') {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
    Serial.println("buzzer beep");
    return;
  }
  if (c == '5') {
    showColor(PIN_R, "red");
    return;
  }
  if (c == '6') {
    showColor(PIN_G, "green");
    return;
  }
  if (c == '7') {
    showColor(PIN_B, "blue");
    return;
  }
  if (c == 'o' || c == 'O') {
    outputsOff();
    Serial.println("outputs off");
    return;
  }
  if (c == 't' || c == 'T') {
    Serial.println("saving empty-scale zero...");
    if (captureZero()) {
      Serial.print("zero saved: ");
      Serial.println(tareOffset);
    } else {
      Serial.println("tare failed");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_DT, INPUT);
  pinMode(PIN_WHITE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  digitalWrite(PIN_SCK, LOW);
  outputsOff();
  delay(300);

  Serial.println();
  Serial.println("bring-up test");
  prefs.begin(NVS_NS, false);
  if (prefs.isKey(NVS_KEY)) {
    tareOffset = prefs.getLong(NVS_KEY, 0);
    tareValid = true;
    Serial.print("loaded zero ");
    Serial.println(tareOffset);
  } else {
    Serial.println("no saved zero. Press t with the scale empty.");
  }

  hx711PowerUp();
  playOutputs();
  Serial.println("keys: a replay, w white, z buzzer, 5 red, 6 green, 7 blue, o off, t zero");
}

void loop() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c != '\r' && c != '\n') {
      handleKey(c);
    }
  }

  const long raw = hx711ReadRaw();
  if (raw == READ_ERROR) {
    Serial.println("scale: no data. Check DT GPIO4, SCK GPIO3, 3V3, GND.");
  } else {
    Serial.print("raw = ");
    Serial.print(raw);
    if (tareValid) {
      Serial.print("  zeroed = ");
      Serial.println(raw - tareOffset);
    } else {
      Serial.println();
    }
  }
  delay(500);
}
