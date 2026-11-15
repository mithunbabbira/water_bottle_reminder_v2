// Minimal ESP32-WROOM pin check. No scale, no RGB library.
// GPIO26 held HIGH forever after boot (~3.3V to GND).
// GPIO25 blinks so you can see the board is alive.
// Serial 115200: 1 = 26 HIGH, 0 = 26 LOW, t = toggle 26

static const int PIN_BUZZER = 26;
static const int PIN_WHITE = 25;

bool high = true;

void apply() {
  digitalWrite(PIN_BUZZER, high ? HIGH : LOW);
  Serial.print("GPIO26 = ");
  Serial.print(high ? "HIGH" : "LOW");
  Serial.print("  digitalRead=");
  Serial.print(digitalRead(PIN_BUZZER));
  Serial.println(high ? "  meter 26-to-GND should be ~3.3V" : "  meter should be ~0V");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_WHITE, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH);
  high = true;
  Serial.println();
  Serial.println("gpio26_test");
  Serial.println("buzzer + -> GPIO26, buzzer - -> GND");
  apply();
}

void loop() {
  // Blink white LED to prove firmware is running.
  digitalWrite(PIN_WHITE, HIGH);
  delay(250);
  digitalWrite(PIN_WHITE, LOW);
  delay(250);

  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '1') {
      high = true;
      apply();
    } else if (c == '0') {
      high = false;
      apply();
    } else if (c == 't' || c == 'T') {
      high = !high;
      apply();
    }
  }
}
