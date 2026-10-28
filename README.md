# water_bottle_reminder_v2

A smart water-bottle base that notices when you drink, reminds you when you have not, and stays quiet when you are away or it is quiet hours.

The running firmware is the Arduino sketch in `bottle/`. It runs on an ESP32-C3 Super Mini. The bottle sits on a load cell. The base weighs it, talks to one paired phone over Bluetooth, and serves a setup page on your home Wi-Fi.

This is a standalone bottle. It does not use the older house-automation ESP-NOW link to a Raspberry Pi. The drink rules follow that earlier `hydration_v3` design: the same thresholds, the same lift and return behavior, and the same light and buzzer timing. Notifications stay on the bottle itself.

## Objective

Help one person drink through the day without nagging them when they are not there.

- Learn how heavy the bottle is, then treat a real drop in weight as a drink and a real rise as a refill.
- Remind with light first, then sound, only while the bottle is on the base and the paired phone is still around.
- Stop the moment the bottle is picked up, a drink or refill is confirmed, quiet hours start, Do Not Disturb is on, or the phone leaves.
- Let the owner set color, quiet hours, reminder interval, beep style, and Wi-Fi from a phone on the same network, without joining a setup hotspot every time.

## Hardware

| Part | Connection |
| --- | --- |
| ESP32-C3 Super Mini | USB serial at 115200 |
| HX711, 10 kg load cell | DT GPIO4, SCK GPIO3 |
| Common-anode RGB LED, 3.3 V | R GPIO5, G GPIO6, B GPIO7, pin LOW = on |
| White LED | GPIO1, HIGH = on |
| Active buzzer | GPIO10, HIGH = on |

GPIO2, GPIO8, and GPIO9 are left empty. Boot never writes a new zero by itself.

## How a drink is detected

Grams are `(raw − tare) / calibration`. The empty base is zeroed once with `t` or the page button. A known weight is calibrated with `c <grams>`.

The base waits 1.5 seconds after boot, then needs 3 samples, 120 ms apart, before it believes a change.

- **Lifted.** Weight stays under 80 g. The saved baseline is kept, except at boot: if the bottle is already missing, the baseline is cleared and learned again when the bottle comes back.
- **Returned.** Weight stays over 88 g. The base waits 2 seconds for the water to settle.
- **Confirmed weight.** Two readings 250 ms apart. If they agree within 5 g, their average is used. If they do not, a third reading is taken and the median is used.
- **Drink.** The bottle is at least 50 g lighter than the baseline. That difference is added to today’s millilitres (1 g is treated as 1 ml). The baseline moves to the new weight. Any reminder stops, and the interval starts again.
- **Refill.** The bottle is at least 100 g heavier. The baseline moves up. Today’s total does not increase. Any reminder stops, and the interval starts again.
- **Put back without drinking.** The change is between those two limits. The baseline stays. If a reminder was already running when the bottle was lifted, it starts again after the 2 second settle. A small sip under 50 g is kept in the baseline until later sips add up.

If the bottle is on the base at boot and the saved baseline is off by 100 g or more, the baseline is moved to the current weight without counting a drink.

At midnight, once the clock is valid, today’s total resets.

## How you are notified

Nothing beeps during the first 5 seconds. The light blinks alone so the bottle can be picked up quietly. After that, the buzzer follows the saved beep style unless Do Not Disturb or quiet hours are on.

| State | Light | Sound |
| --- | --- | --- |
| Idle, daytime | Solid color chosen on the page (default purple) | Off |
| Quiet hours | Solid blue | Off |
| Drink reminder | Cyan and white LED, blinking every 500 ms | After 5 seconds, on the off-beat of the blink |
| Bottle missing for 3 minutes | Red and white LED, same blink | After 5 seconds, same beep rules |

Beep styles are Off, Single, Beep beep, and Long. Beep beep is two short chirps. The buzzer sounds while the LEDs are off so the 3.3 V rail is not driving both at full strength.

The drink reminder starts when the bottle has been on the base, the interval has passed (default 30 minutes, set from the page, 1–240), it is not quiet hours, and the phone check allows it. The missing alert starts only after the bottle has been off the base for 3 minutes.

## When the notification stops

- The bottle is lifted.
- A drink of at least 50 g, or a refill of at least 100 g, is confirmed.
- The bottle is put back after a missing alert.
- The paired phone is no longer here. Reminders turn off, and the 3 minute missing timer does not run while you are away.
- Quiet hours begin. The default window is 23:00–10:00 and can be changed on the page.
- Do Not Disturb is on. The light can still show the alert color. The buzzer stays off.
- The scale is zeroed. The baseline and any active reminder are cleared.

Until a phone has been paired, reminders are allowed without a presence check. After pairing, the bottle remembers that phone’s identity key. It advertises again only when that phone disconnects, and a different phone replaces the previous bond. “Here” means the phone is connected, or it was seen within the last 90 seconds.

## Setup page

With no saved Wi-Fi, the bottle opens an open network named `bottle` at `http://192.168.4.1/`. After a 2.4 GHz network is saved, it joins that network and the page is served on the address the router gives it, for example `http://192.168.1.3/`. The Mac or phone stays on the normal Wi-Fi, which still has internet. Up to four networks can be saved. At boot the bottle scans and joins the saved network it can hear. The setup network comes back only if the saved network does not connect, following the behavior chosen on the page.

The page shows live weight, whether the bottle is on the base, today’s millilitres, and the last drink. It can zero the scale, search for a nearby phone, search for nearby 2.4 GHz networks, and save Do Not Disturb, light color, quiet hours, interval, beep style, and Wi-Fi. The ESP32-C3 only hears 2.4 GHz, not 5 GHz.

The clock uses NTP in IST (UTC+5:30) after Wi-Fi connects. Without a clock, quiet hours and the midnight reset stay off and the bottle runs as daytime.

## Serial commands

Open the USB port at 115200. Opening the port resets the ESP32-C3.

| Command | Effect |
| --- | --- |
| `t` | Zero the empty base and clear the baseline |
| `c <grams>` | Calibrate from a known weight after zeroing |
| `w` | Print the weight now |
| `p` | Advertise as Bottle so a phone can pair |
| `b` | Search nearby Bluetooth names. `b ultra` filters by name |
| `s <number>` | Save a phone from the last search |
| `n` | Scan 2.4 GHz Wi-Fi |
| `j <ssid>\|<password>` | Save a network and join it |
| `r <ssid>` | Forget one saved network |
| `a` | Print the same status JSON the page uses |
| `h` | Print this list |

## Layout

| Path | Role |
| --- | --- |
| `bottle/bottle.ino` | Boot, serial commands, Bluetooth pairing and presence |
| `bottle/Hydration.h` | Drink, refill, reminder, and quiet-hours state machine |
| `bottle/Outputs.h` | RGB, white LED, and buzzer |
| `bottle/Scale.h` | HX711 reads, tare, and calibration |
| `bottle/Portal.h` | Wi-Fi, setup access point, and the web server |
| `bottle/WebPage.h` | The setup page |
| `bottle/BottleNvm.h` | Saved zero, calibration, baseline, totals, phone, and Wi-Fi |
| `bottle/Config.h` | Pins and the drink thresholds |
| `bringup_test/`, `hx711_test/`, `rgb_touch_test/` | Early hardware checks, not the product firmware |

Build the `bottle` sketch with the ESP32 Arduino core for `nologo_esp32c3_super_mini` and `PartitionScheme=huge_app`.
