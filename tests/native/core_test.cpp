#include "Hydration.h"
#include "PhonePresence.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <cmath>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #x); } while (0)
#define NEAR(a,b) CHECK(std::fabs((a) - (b)) < 0.1f)

// Native HX711 transport double: exercise the actual Scale and Hydration adapter
// without wires or hardware. These definitions never enter firmware builds.
static long adcRaw = 700;
static bool adcReady = true;
HX711::HX711() = default;
HX711::~HX711() = default;
void HX711::begin(byte, byte, byte) {}
bool HX711::is_ready() { return adcReady; }
long HX711::read() { return adcRaw; }
void HX711::set_scale(float scale) { SCALE = scale; }
float HX711::get_scale() { return SCALE; }
void HX711::set_offset(long offset) { OFFSET = offset; }
long HX711::get_offset() { return OFFSET; }

struct Rig {
  HydrationEngine e;
  HydrationSettings s;
  HydrationInput in;
  uint32_t now;
  explicit Rig(uint32_t start = 0, bool saved = false, float base = 700, float total = 0) : now(start) {
    in.phoneHere = true;
    in.scaleValid = true; in.grams = 700; in.clockValid = in.activeWindow = true; in.day = 20261115;
    e.begin(now, saved, base, total, 20261115);
  }
  void step(float grams, uint32_t ms = 100, bool fresh = true) {
    now += ms; in.grams = grams; if (fresh) ++in.sequence; e.tick(now, s, in);
  }
  void hold(uint32_t ms, float grams) { while (ms) { const uint32_t dt = ms > 100 ? 100 : ms; step(grams, dt); ms -= dt; } }
  void boot(float grams = 700) { hold(2300, grams); CHECK(e.ready()); }
  void reminder(float grams = 700) {
    const uint32_t start = now;
    while (!e.alerting() && now - start < s.intervalMs + 1000) step(grams);
    CHECK(e.alerting()); CHECK(std::string(e.stateName()) == "reminder");
  }
  void lift() { step(0); CHECK(!e.frame().white && !e.frame().beep && !e.frame().color); step(0); CHECK(e.missing()); }
  void returned(float grams) { hold(3000, grams); CHECK(!e.missing()); }
};

static void warmupDistinct() {
  Rig r;
  r.step(700, 1500); CHECK(!r.e.ready());
  for (int i = 0; i < 3; ++i) r.step(700, 100, false);
  CHECK(!r.e.ready()); CHECK(!r.e.hasBaseline());
  r.step(700); r.step(700); r.hold(500, 700);
  CHECK(r.e.hasBaseline()); NEAR(r.e.baseline(), 700); CHECK(!r.e.hasLastDrink());
  Rig absent(0, true); absent.boot(0); CHECK(absent.e.missing()); CHECK(absent.e.hasBaseline());
  NEAR(absent.e.baseline(), 700); absent.returned(400); NEAR(absent.e.baseline(), 400); NEAR(absent.e.dailyMl(), 0);
}
static void liftReturnNoDrink() {
  Rig r; r.boot(); r.reminder(); CHECK(r.e.frame().color == 0x00ffff); CHECK(r.e.frame().white);
  r.lift(); r.hold(300, 700); CHECK(!r.e.alerting());
  r.hold(1800, 700); CHECK(!r.e.alerting());
  r.hold(500, 700); CHECK(r.e.alerting()); NEAR(r.e.dailyMl(), 0);
  CHECK(r.e.takeEvents() & HydrationEngine::Returned);
}
static void drinkRefillSips() {
  Rig r; r.boot(); r.reminder(); r.lift(); r.returned(650);
  CHECK(!r.e.alerting()); NEAR(r.e.dailyMl(), 50); NEAR(r.e.lastDrinkMl(), 50);
  CHECK(r.e.frame().color == 0x00ff00); CHECK(r.e.nextCheckSeconds(r.now) >= 1798);
  r.hold(2000, 650); CHECK(r.e.frame().color == r.s.idleColor);
  r.lift(); r.returned(750); NEAR(r.e.dailyMl(), 50); NEAR(r.e.baseline(), 750);
  CHECK(r.e.takeEvents() & HydrationEngine::Refill);
  for (float g : {730.0f, 710.0f}) { r.lift(); r.returned(g); NEAR(r.e.dailyMl(), 50); }
  r.lift(); r.returned(690); NEAR(r.e.dailyMl(), 110); NEAR(r.e.lastDrinkMl(), 60);
}
static void missingQuietDndAway() {
  PhonePresence p; CHECK(!p.here(0)); CHECK(!p.here(100000));
  p.connected(100000); CHECK(p.here(999999));
  p.disconnected(100001); CHECK(p.here(160000)); CHECK(!p.here(160001));
  p.connected(160002); CHECK(p.here(160002));
  p.disconnected(0xfffffff0u); CHECK(p.here(uint32_t(0xfffffff0u + 59999u)));
  CHECK(!p.here(uint32_t(0xfffffff0u + 60000u)));
  Rig r; r.s.missingMs = 10000; r.s.beepDelayMs = 0; r.s.missingColor = 0x123456;
  r.boot(); r.lift(); r.hold(10300, 0); CHECK(r.e.alerting()); CHECK(r.e.frame().color == 0x123456);
  r.hold(500, 0); CHECK(r.e.frame().beep);
  r.s.dnd = true; r.step(0); CHECK(r.e.alerting()); CHECK(!r.e.frame().beep);
  r.in.activeWindow = false; r.step(0); CHECK(!r.e.alerting()); CHECK(r.e.frame().color == 255); CHECK(!r.e.frame().white);
  r.in.activeWindow = true; r.in.clockValid = false; r.step(0); CHECK(!r.e.alerting()); CHECK(r.e.frame().color == 255);
  r.in.clockValid = true; r.step(0); CHECK(r.e.alerting());
  r.in.phoneHere = false; r.step(0); CHECK(!r.e.alerting()); CHECK(!r.e.frame().beep && !r.e.frame().white);
  r.in.phoneHere = true; r.step(0); CHECK(r.e.alerting());
  r.in.phoneHere = false; r.step(0); CHECK(!r.e.alerting());
  Rig bootAway; bootAway.in.phoneHere = false; bootAway.boot();
  bootAway.hold(bootAway.s.intervalMs + 1000, 700); CHECK(!bootAway.e.alerting());
  bootAway.in.phoneHere = true; bootAway.step(700); CHECK(bootAway.e.alerting());
  bootAway.in.phoneHere = false; bootAway.step(700); CHECK(!bootAway.e.alerting());
}
static void classicPresenceDemandAndDrinkFeedback() {
  Rig r; r.s.intervalMs = 2000; r.in.phoneHere = false;
  CHECK(!r.e.needsPresence(r.now)); r.boot(); CHECK(!r.e.needsPresence(r.now));
  r.hold(2500, 700); CHECK(!r.e.alerting()); CHECK(r.e.needsPresence(r.now));
  CHECK(!r.e.frame().white && !r.e.frame().beep && r.e.frame().color != r.s.reminderColor);
  r.in.phoneHere = true; r.step(700); CHECK(r.e.alerting() && r.e.needsPresence(r.now));
  r.in.phoneHere = false; r.step(700); CHECK(!r.e.alerting() && r.e.needsPresence(r.now));
  CHECK(!r.e.needsPresence(r.now + 401));
  r.in.activeWindow = false; r.step(700); CHECK(!r.e.needsPresence(r.now));
  r.in.activeWindow = true; r.in.clockValid = false; r.step(700); CHECK(!r.e.needsPresence(r.now));
  r.in.clockValid = true; r.step(700); CHECK(r.e.needsPresence(r.now));
  r.lift(); CHECK(!r.e.needsPresence(r.now)); r.returned(640);
  CHECK(r.e.hasLastDrink() && r.e.frame().color == 0x00ff00);
  CHECK(!r.e.frame().white && !r.e.frame().beep && !r.e.needsPresence(r.now));
  r.step(640); CHECK(r.e.frame().color == 0x00ff00); // Still green away on subsequent ticks.
  r.in.scaleFault = true; r.step(640); CHECK(!r.e.needsPresence(r.now) && r.e.frame().color == 0);

  Rig m; m.s.missingMs = 1000; m.in.phoneHere = false; m.boot(); m.lift();
  m.hold(800, 0); CHECK(!m.e.needsPresence(m.now));
  m.hold(200, 0); CHECK(m.e.needsPresence(m.now) && !m.e.alerting());
  m.in.phoneHere = true; m.step(0); CHECK(m.e.alerting() && m.e.needsPresence(m.now));
  m.in.phoneHere = false; m.step(0); CHECK(!m.e.alerting() && m.e.needsPresence(m.now));
  m.step(700); CHECK(!m.e.needsPresence(m.now)); // First physical return suppresses demand.
  m.in.scaleValid = false; m.step(700); CHECK(!m.e.needsPresence(m.now));
}
static void quietStillTracks() {
  Rig r; r.boot(); r.reminder(); r.in.clockValid = false; r.step(700);
  CHECK(!r.e.alerting()); CHECK(r.e.frame().color == 255);
  // Quiet lift is deliberately blue rather than a drink/missing indication.
  r.hold(300, 0); r.returned(640); NEAR(r.e.dailyMl(), 60); CHECK(r.e.frame().color == 255);
  CHECK(!r.e.frame().white && !r.e.frame().beep);
}
static void scaleFailsClosed() {
  Rig r; r.boot(); r.reminder();
  r.hold(100, 700); r.step(700, 401, false);
  CHECK(r.e.scaleError()); CHECK(!r.e.alerting()); CHECK(!r.e.frame().color && !r.e.frame().white && !r.e.frame().beep);
  r.step(700, 100, false); CHECK(r.e.scaleError());  // No fake recovery on cached conversion.
  r.hold(3000, 700); CHECK(!r.e.scaleError()); CHECK(!r.e.alerting());
  r.in.scaleFault = true; r.step(700); CHECK(r.e.scaleError());
  r.in.scaleFault = false; r.in.scaleValid = false; r.step(700); CHECK(r.e.scaleError());
  r.in.scaleValid = true; r.step(std::numeric_limits<float>::quiet_NaN()); CHECK(r.e.scaleError());
  r.step(std::numeric_limits<float>::infinity()); CHECK(r.e.scaleError());
}
static void bootOfflineNoIntake() {
  for (float g : {400.f, 650.f, 750.f, 900.f}) {
    Rig r(0, true, 700, 25); r.boot(g); NEAR(r.e.dailyMl(), 25); CHECK(!r.e.hasLastDrink());
    NEAR(r.e.baseline(), std::fabs(700 - g) >= 100 ? g : 700);
    r.reminder(g); NEAR(r.e.dailyMl(), 25); r.lift(); r.returned(g - 50); NEAR(r.e.dailyMl(), 75);
  }
}
static void movingWaterBoundedRetry() {
  Rig r; r.boot(); r.reminder(); r.lift(); r.hold(2200, 650);
  for (int i = 0; i < 60; ++i) r.step(i % 2 ? 660 : 640);
  CHECK(!r.e.alerting()); NEAR(r.e.dailyMl(), 0); CHECK(std::string(r.e.stateName()) == "idle");
  CHECK(r.e.nextCheckSeconds(r.now) <= 5);
  r.hold(6000, 650); NEAR(r.e.dailyMl(), 50); CHECK(!r.e.alerting());
  // Intermittent low base readings during settling cancel confirmation, not a 600 ml drink.
  r.lift(); r.hold(2200, 650); r.step(90); r.step(90); r.returned(650); NEAR(r.e.dailyMl(), 50);
}
static void stabilityAndDistinctConfirmation() {
  Rig r; r.boot(); r.lift(); r.hold(2000, 650);
  for (int i = 0; i < 4; ++i) r.step(650, 100, false);
  NEAR(r.e.dailyMl(), 0);
  r.hold(1000, 650); NEAR(r.e.dailyMl(), 50);
  r.lift(); r.hold(2200, 600);
  for (int i = 0; i < 15; ++i) r.step(i % 3 == 0 ? 610 : 600);
  NEAR(r.e.dailyMl(), 50); r.hold(1000, 600); NEAR(r.e.dailyMl(), 100);
}
static void fastAdcAndConfiguredThresholds() {
  Rig r; r.s.drinkGrams = 75; r.s.settleMs = 500;
  for (int i = 0; i < 200; ++i) r.step(700, 13);  // Approximately 80 Hz.
  CHECK(r.e.hasBaseline());
  for (int i = 0; i < 3; ++i) r.step(0, 13);
  for (int i = 0; i < 100; ++i) r.step(650, 13);
  NEAR(r.e.dailyMl(), 0);
  for (int i = 0; i < 3; ++i) r.step(0, 13);
  for (int i = 0; i < 100; ++i) r.step(625, 13);
  NEAR(r.e.dailyMl(), 75);
  r.in.sequence = UINT32_MAX - 1;
  for (int i = 0; i < 3; ++i) r.step(0, 13);  // Conversion id wrap is also distinct.
  CHECK(r.e.missing());
}
static void defaultMissingAndBeepDelay() {
  Rig r; r.boot(); r.lift(); r.hold(179000, 0); CHECK(!r.e.alerting());
  r.hold(1200, 0); CHECK(r.e.alerting()); CHECK(r.e.frame().color == 0xff0000);
  r.hold(4800, 0); CHECK(!r.e.frame().beep);
  r.hold(600, 0); CHECK(r.e.frame().beep);
  r.s.dnd = true; r.step(0); CHECK(!r.e.frame().beep && r.e.alerting());
}
static void intervalChangesAndTare() {
  Rig unchanged; unchanged.boot(); const uint32_t before = unchanged.e.nextCheckSeconds(unchanged.now);
  unchanged.lift(); unchanged.returned(680);
  CHECK(unchanged.e.nextCheckSeconds(unchanged.now) < before); NEAR(unchanged.e.dailyMl(), 0);
  Rig r; r.boot(); r.reminder(); r.s.intervalMs = 60000; r.step(700);
  CHECK(!r.e.alerting()); CHECK(r.e.nextCheckSeconds(r.now) == 60);
  r.hold(59000, 700); CHECK(!r.e.alerting()); r.hold(1500, 700); CHECK(r.e.alerting());
  r.e.restartInterval(r.now, 60000); CHECK(!r.e.alerting()); CHECK(r.e.nextCheckSeconds(r.now) == 60);
  r.e.resetAfterTare(r.now); CHECK(!r.e.hasBaseline()); CHECK(!r.e.alerting());
  r.hold(2300, 0); CHECK(r.e.missing()); CHECK(!r.e.hasBaseline());
  r.returned(700); CHECK(r.e.hasBaseline()); NEAR(r.e.dailyMl(), 0);
}
static void rolloverAndDay() {
  Rig r(UINT32_MAX - 2000); r.boot(); CHECK(r.now < 1000); r.reminder(); r.lift(); r.returned(640);
  NEAR(r.e.dailyMl(), 60); r.in.day = 20261116; r.step(640); NEAR(r.e.dailyMl(), 0);
  r.in.clockValid = false; r.in.day = 20261117; r.step(640); CHECK(r.e.lastDay() == 20261116);
  // Alarm beep delay and flash timers also cross uint32_t wrap.
  Rig q(UINT32_MAX - 8000); q.s.intervalMs = 2000; q.boot(); q.reminder();
  q.lift(); q.returned(650); NEAR(q.e.dailyMl(), 50); CHECK(q.e.frame().color == 0x00ff00);
  q.hold(1000, 650); CHECK(q.now < 1000); CHECK(q.e.frame().color == 0x00ff00);
  q.hold(600, 650); CHECK(q.e.frame().color == q.s.idleColor);
  Rig beep(UINT32_MAX - 8000); beep.s.intervalMs = 2000; beep.boot(); beep.reminder();
  beep.hold(5500, 700); CHECK(beep.now < 2000); CHECK(beep.e.frame().beep);
}
static BottleNvm freshNvm() { Preferences::reset(); BottleNvm n; CHECK(n.begin()); n.load(); return n; }
static void nvmAtomicSettings() {
  BottleNvm n = freshNvm(); CHECK(!n.settingsStored()); CHECK(!n.stored("settings_v2")); CHECK(!n.settingStored("interval_min"));
  auto d = n.data; d.intervalMin = 45; d.missingSec = 240; d.reminderColor = 0xabcdef; d.presenceEnabled = true;
  unsigned writes = Preferences::state().writes; CHECK(n.saveSettings(d)); CHECK(Preferences::state().writes == writes + 1);
  CHECK(n.stored("settings_v2") && n.settingStored("settle_ms")); CHECK(Preferences::state().lastWrite == "settings_v2");
  Preferences::state().fail = true; d.intervalMin = 90; CHECK(!n.saveSettings(d)); CHECK(n.data.intervalMin == 45);
  Preferences::state().fail = false;
  BottleNvm again; again.begin(); again.load(); CHECK(again.data.intervalMin == 45); CHECK(again.data.presenceEnabled);
  for (int field = 0; field < 12; ++field) {
    d = n.data;
    switch(field) {
      case 0: d.intervalMin = 241; break; case 1: d.startMin = 1440; break; case 2: d.endMin = 1440; break;
      case 3: d.tzMin = 1; break; case 4: d.missingSec = 9; break; case 5: d.beepDelaySec = 31; break;
      case 6: d.drinkGrams = 4; break; case 7: d.settleMs = 499; break; case 8: d.reminderColor = 0x1000000; break;
      case 9: d.missingColor = 0x1000000; break; case 10: d.beepType = 4; break;
      case 11: d.presenceEnabled = false; break;
    }
    CHECK(!n.saveSettings(d)); CHECK(n.data.intervalMin == 45);
  }
}
static void nvmMigrationAndInvalidValues() {
  Preferences::reset(); Preferences p; p.begin("bottle", false);
  p.putBool("presence_enabled", false);
  p.putLong("tare_offset", 123); p.putFloat("cal_factor", -2000); p.putFloat("baseline_weight", 700);
  p.putFloat("daily_total", 150); p.putInt("last_day", 20261115); p.putUShort("interval_min", 40);
  p.putUShort("start_min", 65535); p.putUChar("beep_type", 255); p.putShort("tz_min", -30);
  p.putString("wifi_s0", "a wifi with spaces"); p.putString("wifi_p0", "a password");
  BottleNvm n; n.begin(); n.load(); CHECK(n.settingsStored()); CHECK(n.data.intervalMin == 40); CHECK(n.data.presenceEnabled);
  CHECK(n.data.startMin == 600 && n.data.beepType == 2 && n.data.tzMin == -30);
  CHECK(n.data.hasCal && n.data.hasTare && n.data.hasBaseline); NEAR(n.data.dailyTotalMl, 150);
  CHECK(n.data.netCount == 1); CHECK(n.data.lastDay == 20261115); CHECK(n.rawStored("interval_min"));
  // A corrupt new blob must not cause loss of calibration, networks or old settings.
  Preferences::state().keys["settings_v2"].bytes[0] ^= 0x80;
  n.load(); CHECK(n.data.intervalMin == 40 && n.data.netCount == 1); NEAR(n.data.calFactor, -2000);
  p.putFloat("cal_factor", NAN); p.putFloat("baseline_weight", INFINITY); p.putFloat("daily_total", -1);
  n.load(); CHECK(!n.data.hasCal && !n.data.hasBaseline); NEAR(n.data.dailyTotalMl, 0);
  Preferences::reset(); p.putUShort("interval_min", 0); Preferences::state().fail = true;
  n.load(); CHECK(n.data.intervalMin == 30); CHECK(!n.settingUsable("interval_min")); CHECK(n.stored("interval_min"));
  CHECK(!n.settingsStored()); Preferences::state().fail = false;
}
static void nvmFailuresAndNetworks() {
  BottleNvm n = freshNvm(); CHECK(n.saveCal(-2000)); CHECK(n.saveTare(123)); CHECK(n.saveHydration(true, 700, 100, 20261115));
  const uint8_t mac[6] = {1,2,3,4,5,6}; CHECK(n.saveBleMac(mac));
  CHECK(n.saveNet("wifi one", "password")); CHECK(n.saveNet("wifi two", "password"));
  CHECK(n.saveNet("wifi three", "password")); CHECK(n.saveNet("wifi four", ""));
  CHECK(!n.saveNet("fifth", "password")); CHECK(n.data.netCount == 4);
  CHECK(n.saveNet("wifi one", "")); CHECK(n.data.nets[0].pass[0] == 0); CHECK(n.stored("wifi_p0"));
  Preferences::state().fail = true;
  CHECK(!n.saveTare(456)); CHECK(n.data.tareOffset == 123);
  CHECK(!n.saveCal(2000)); NEAR(n.data.calFactor, -2000);
  CHECK(!n.clearBaseline()); CHECK(n.data.hasBaseline); CHECK(!n.saveTotals(200, 20261116)); NEAR(n.data.dailyTotalMl, 100);
  CHECK(!n.removeNet("wifi one")); CHECK(n.data.netCount == 4); CHECK(!n.clearNets());
  CHECK(!n.clearBleMac()); CHECK(n.data.hasBleMac);
  CHECK(!n.saveNet("wifi one", "newpassword")); CHECK(n.data.nets[0].pass[0] == 0);
  Preferences::state().fail = false;
  CHECK(!n.saveCal(NAN) && !n.saveCal(INFINITY) && !n.saveCal(0));
  CHECK(!n.saveBaseline(NAN) && !n.saveBaseline(99) && !n.saveTotals(INFINITY, 0));
  CHECK(!n.saveHydration(false, NAN, 0, 0)); CHECK(!n.saveTotals(0, 20260229));
  BottleNvm reload; reload.begin(); reload.load(); CHECK(reload.data.netCount == 4); NEAR(reload.data.dailyTotalMl, 100);
  CHECK(reload.clearNets()); reload.load(); CHECK(reload.data.netCount == 0); CHECK(!reload.stored("wifi_s0"));
}
static void nvmPeerTombstone() {
  Preferences::reset(); Preferences p;
  const uint8_t mac[6] = {1,2,3,4,5,6}; const uint8_t irk[16] = {8};
  p.putBytes("ble_mac", mac, sizeof(mac)); p.putBytes("ble_irk", irk, sizeof(irk));
  BottleNvm n; n.begin(); n.load(); CHECK(n.data.hasBleMac && n.data.hasIrk);
  Preferences::state().fail = true; CHECK(!n.clearBleMac()); CHECK(n.data.hasBleMac && n.data.hasIrk);
  Preferences::state().fail = false; const unsigned writes = Preferences::state().writes;
  CHECK(n.clearBleMac()); CHECK(Preferences::state().writes == writes + 1);
  n.load(); CHECK(!n.data.hasBleMac && !n.data.hasIrk); CHECK(!n.stored("ble_mac"));
  CHECK(n.rawStored("ble_mac")); CHECK(n.saveBleMac(mac)); n.load(); CHECK(n.data.hasBleMac && !n.data.hasIrk);
}
static void clockFixedOffset() {
  char tz[32]; CHECK(ClockService::tzString(30, tz, sizeof(tz))); CHECK(std::string(tz) == "UTC-0:30");
  CHECK(ClockService::tzString(-30, tz, sizeof(tz))); CHECK(std::string(tz) == "UTC+0:30");
  time_t epoch; CHECK(ClockService::localToEpoch(2026, 11, 15, 0, 15, 0, 330, &epoch));
  Clock c = ClockService::atEpoch(epoch, 330); CHECK(c.day == 20261115 && c.hour == 0 && c.minute == 15);
  c = ClockService::atEpoch(epoch, -30); CHECK(c.day == 20261114 && c.hour == 18 && c.minute == 15);
  setenv("TZ", "UTC+12", 1); tzset(); c = ClockService::atEpoch(epoch, 330); CHECK(c.hour == 0 && c.minute == 15);
  CHECK(!ClockService::atEpoch(std::numeric_limits<time_t>::max(), 840).valid);
  CHECK(!ClockService::localToEpoch(2025, 2, 29, 1, 0, 0, 0, &epoch));
  CHECK(ClockService::localToEpoch(2024, 2, 29, 1, 0, 0, 0, &epoch));
  BottleNvmData d; d.startMin = 1380; d.endMin = 600;
  c.valid = true; c.hour = 23; c.minute = 0; CHECK(ClockService::inActiveWindow(d, c));
  c.hour = 10; CHECK(!ClockService::inActiveWindow(d, c)); c.hour = 9; CHECK(ClockService::inActiveWindow(d, c));
  d.endMin = d.startMin; CHECK(!ClockService::inActiveWindow(d, c)); d.endMin = 600; c.valid = false;
  CHECK(!ClockService::inActiveWindow(d, c));
}
static void adapterPersistenceAndStale() {
  BottleNvm n = freshNvm(); CHECK(n.saveTare(0)); CHECK(n.saveCal(1));
  Hydration h; Scale scale; Outputs out; Clock c; c.valid = true; c.hour = 12; c.day = 20261115;
  NativeArduino::now() = 0; NativeArduino::delays() = 0; h.begin(n.data); scale.begin(); out.begin();
  auto step = [&](uint32_t dt, long grams, bool sample) {
    NativeArduino::now() += dt; adcRaw = grams; adcReady = sample;
    scale.poll(n.data); h.tick(millis(), scale, n, out, c, true);
  };
  for (int i = 0; i < 25; ++i) step(100, 700, true);
  CHECK(h.hasBaseline()); NEAR(n.data.baselineWeight, 700);
  h.setCheckSeconds(1, n); for (int i = 0; i < 15; ++i) step(100, 700, true);
  CHECK(h.alerting() && h.needsPresence(millis()));
  step(100, 0, true); CHECK(!h.alerting()); CHECK(NativeArduino::digital()[PIN_WHITE] == LOW);
  CHECK(NativeArduino::digital()[PIN_BUZZER] == LOW); step(100, 0, true);
  Preferences::state().fail = true;
  for (int i = 0; i < 30; ++i) step(100, 650, true);
  NEAR(h.dailyMl(), 50); CHECK(h.persistenceFailed() && h.persistencePending()); NEAR(n.data.dailyTotalMl, 0);
  Preferences::state().fail = false;
  for (int i = 0; i < 60; ++i) step(100, 650, true);
  CHECK(!h.persistenceFailed() && !h.persistencePending()); NEAR(n.data.dailyTotalMl, 50);
  step(401, 650, false); CHECK(h.scaleError()); CHECK(!h.alerting());
  CHECK(NativeArduino::digital()[PIN_WHITE] == LOW && NativeArduino::digital()[PIN_BUZZER] == LOW);
  CHECK(NativeArduino::delays() == 0);  // Hydration never waits/delays/pumps.
  CHECK(h.forgetBaseline(n)); CHECK(!h.hasBaseline() && !h.alerting());
}
int main() {
  const std::pair<const char *, std::function<void()>> tests[] = {
    {"warmup/distinct/boot-missing", warmupDistinct}, {"lift/return/no drink", liftReturnNoDrink},
    {"drink/refill/accumulated sips", drinkRefillSips}, {"missing/quiet/dnd/away", missingQuietDndAway},
    {"Classic demand/quiet/sensor gates/away drink feedback", classicPresenceDemandAndDrinkFeedback},
    {"quiet tracks drinks", quietStillTracks}, {"scale stale/fault/NaN", scaleFailsClosed},
    {"boot offline no intake", bootOfflineNoIntake}, {"moving-water bounded retry", movingWaterBoundedRetry},
    {"stable distinct confirmation", stabilityAndDistinctConfirmation}, {"80Hz/configured thresholds/sequence wrap", fastAdcAndConfiguredThresholds},
    {"default missing/beep timing", defaultMissingAndBeepDelay}, {"interval change/tare", intervalChangesAndTare},
    {"millis rollover/day", rolloverAndDay}, {"NVS atomic settings", nvmAtomicSettings},
    {"NVS migration/invalid values", nvmMigrationAndInvalidValues}, {"NVS failures/networks", nvmFailuresAndNetworks},
    {"NVS peer atomic clear", nvmPeerTombstone}, {"clock fixed offset/window", clockFixedOffset},
    {"adapter persistence/stale/no waits", adapterPersistenceAndStale}
  };
  unsigned passed = 0;
  for (const auto &test : tests) {
    try { test.second(); ++passed; std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception &e) { std::cerr << "FAIL " << test.first << ": " << e.what() << '\n'; return 1; }
  }
  std::cout << passed << " tests passed\n";
}
