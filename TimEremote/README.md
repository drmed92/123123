# TimEremote

An ultra-low-power, **schedule-only** IR remote for split ACs — a fork of
ERemote stripped down for battery use. No home Wi-Fi, no cloud, no generator
detection. It records your AC remote's buttons, holds a weekly schedule, and
deep-sleeps almost all the time, waking only to fire a scheduled command.

## Required wiring (ESP-12E / ESP-12F)

| Signal | Pin | Notes |
|---|---|---|
| **GPIO16 → RST** | D0 → RST | **Mandatory** for deep-sleep timer wake. The on-board RST *button* does not do this — the timer wakes the chip by pulsing RST through this wire. Without it the device sleeps and never wakes. |
| IR LED | GPIO4 (D2) | via 2N2222: GPIO4 → ~220Ω → base, emitter → GND, collector → LED cathode, LED anode → +3V3 through ~100Ω |
| IR receiver (VS1838B) | GPIO14 (D5) | OUT → GPIO14, VCC 3V3, GND |

Change `IR_TX_PIN` / `IR_RX_PIN` at the top of the sketch if you wire differently.

Libraries: **ArduinoJson v7**, **IRremoteESP8266**.

## Using it

1. **Power on** (or press RST). The device brings up Wi-Fi **ERemoteXX**
   (password `88888888`) for **5 minutes**. Connect and open **http://4.4.4.4**.
2. Set the current date & time, record the ON / OFF / ECO buttons from your AC
   remote (point it at the receiver and press each), and add schedule entries
   (action + time + weekdays). Optionally tick "ECO: turn the AC on first".
3. Tap **Save & sleep now**, or just wait — after 5 minutes it sleeps
   automatically.
4. From then on it wakes only at scheduled times to send the IR command, with
   Wi-Fi off. To change anything, **press RST** to get the programming Wi-Fi
   back.

## Clock accuracy

Time is set from your phone and kept across sleeps in RTC memory. There is no
internet time source, so the RTC oscillator drifts a few minutes per day, and
the clock is **lost on a full power cut** (you'll re-set it on the next boot).
Fine for "turn the AC on around 2 pm"; not second-accurate.

## Power (rough)

WiFi off during sleep, ~5 sends/hour:
- Deep sleep: bare ESP-12 ~20 µA (~0.02 mAh/h); a Wemos D1 mini board's USB
  chip pushes this to ~0.3–0.5 mAh/h.
- 5 sends/hour: ~0.035 mAh/h.
- **≈ 0.05 mAh/h on a bare ESP-12**, ~0.35–0.5 mAh/h on a stock D1 mini.

A single 18650 (~2500 mAh) lasts months to years depending on board. Removing
the USB-serial chip / power LED is the biggest win. Each 5-minute programming
session costs a one-time ~6 mAh.
