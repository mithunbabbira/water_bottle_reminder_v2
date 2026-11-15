// Temporary test: actively contact the paired phone, like the Pi's ping.
// This chip has Bluetooth Low Energy only, so the attempt is a 2 second
// connection to the phone address from pairing, not a classic l2ping.
// Serial 115200. Green = reply. Red = no reply for 10 seconds.

#include <BLEAddress.h>
#include <BLEDevice.h>
#include <Preferences.h>
#include <host/ble_gap.h>

static const int PIN_R = 5;
static const int PIN_G = 6;
static const int PIN_B = 7;
static const uint32_t PING_MS = 2000;
static const uint32_t AWAY_MS = 10000;
static const char *PHONE = "48:ef:1c:49:6a:e7";

volatile int pingStatus = -1;
volatile bool pingDone = false;
uint32_t lastReplyMs = 0;
bool reportedHere = false;

void show(bool red, bool green) {
  digitalWrite(PIN_R, red ? LOW : HIGH);
  digitalWrite(PIN_G, green ? LOW : HIGH);
  digitalWrite(PIN_B, HIGH);
}

int onGap(struct ble_gap_event *event, void *arg) {
  (void)arg;
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    pingStatus = event->connect.status;
    pingDone = true;
    if (event->connect.status == 0) {
      ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
  }
  return 0;
}

bool ping(uint8_t addrType) {
  BLEAddress address(PHONE);
  ble_addr_t peer;
  memcpy(peer.val, address.getNative(), 6);
  peer.type = addrType;
  pingDone = false;
  pingStatus = -1;
  const int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer, PING_MS, nullptr, onGap, nullptr);
  if (rc != 0) {
    return false;
  }
  const uint32_t start = millis();
  while (!pingDone && millis() - start < PING_MS + 300) {
    delay(20);
  }
  if (!pingDone) {
    ble_gap_conn_cancel();
    return false;
  }
  return pingStatus == 0;
}

void setup() {
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  show(true, false);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("phone presence test");
  Serial.print("pinging paired phone ");
  Serial.println(PHONE);
  Serial.println("2 second connection attempt, then repeat");

  Preferences prefs;
  if (prefs.begin("bottle", true)) {
    uint8_t mac[6];
    if (prefs.getBytes("ble_mac", mac, sizeof(mac)) == sizeof(mac)) {
      Serial.print("saved address on the board is ");
      for (int i = 0; i < 6; i++) {
        if (i > 0) {
          Serial.print(':');
        }
        if (mac[i] < 16) {
          Serial.print('0');
        }
        Serial.print(mac[i], HEX);
      }
      Serial.println(" (not used for the ping)");
    }
    prefs.end();
  }

  if (!BLEDevice::init("BottleTest")) {
    Serial.println("bluetooth failed to start");
    return;
  }
  Serial.println("active ping running. turn the phone Bluetooth off, then on.");
}

void loop() {
  Serial.print("ping ");
  Serial.print(PHONE);
  Serial.print(" ... ");
  const bool reply = ping(BLE_ADDR_PUBLIC) || ping(BLE_ADDR_RANDOM);
  const uint32_t now = millis();
  if (reply) {
    lastReplyMs = now;
    Serial.println("reply");
    show(false, true);
    if (!reportedHere) {
      reportedHere = true;
      Serial.println("PAIRED PHONE HERE");
    }
  } else {
    Serial.println("no reply");
    if (reportedHere && now - lastReplyMs >= AWAY_MS) {
      reportedHere = false;
      show(true, false);
      Serial.println("PAIRED PHONE AWAY");
    } else if (!reportedHere) {
      show(true, false);
    }
  }
  delay(300);
}
