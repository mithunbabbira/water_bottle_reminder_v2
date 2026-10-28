// Common-anode RGB touch test for the ESP32-C3 Super Mini.
// Longest leg -> 3V3. Color legs touch GPIO5, GPIO6, GPIO7 directly.
// A pin at LOW lights the LED, same as the old ESP32 firmware.
// Serial 115200. Keys 5, 6, 7 hold that pin on. Any other key resumes the cycle.

static const int COLOR_PINS[] = {5, 6, 7};
static const int COLOR_COUNT = 3;
static const unsigned long STEP_MS = 4000;

int heldPin = -1;

void allOff() {
  for (int i = 0; i < COLOR_COUNT; i++) {
    digitalWrite(COLOR_PINS[i], HIGH);
  }
}

void setOnly(int pin) {
  allOff();
  digitalWrite(pin, LOW);
  Serial.print("ON  GPIO");
  Serial.print(pin);
  Serial.println("  (touch a color leg to this pin)");
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < COLOR_COUNT; i++) {
    pinMode(COLOR_PINS[i], OUTPUT);
    digitalWrite(COLOR_PINS[i], HIGH);
  }
  delay(300);
  Serial.println();
  Serial.println("RGB touch test");
  Serial.println("Longest leg stays on 3V3.");
  Serial.println("GPIO5, GPIO6, GPIO7 take turns. Write down the color of each.");
  Serial.println("Press 5, 6, or 7 to hold one pin. Press any other key to cycle.");
}

void loop() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '5') {
      heldPin = 5;
    } else if (c == '6') {
      heldPin = 6;
    } else if (c == '7') {
      heldPin = 7;
    } else if (c != '\r' && c != '\n') {
      heldPin = -1;
      Serial.println("cycle");
    }
  }

  if (heldPin != -1) {
    setOnly(heldPin);
    delay(300);
    return;
  }

  for (int i = 0; i < COLOR_COUNT; i++) {
    setOnly(COLOR_PINS[i]);
    const unsigned long started = millis();
    while (millis() - started < STEP_MS) {
      if (Serial.available() > 0) {
        return;
      }
      delay(20);
    }
  }
}
