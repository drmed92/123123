# NtpEremote — low-power, NTP-scheduled IR remote

A battery-minded fork of ERemote. It joins your home Wi-Fi for a few seconds
to read the real time from the internet (NTP, **Baghdad UTC+3**), then spends
almost all its life in deep sleep, waking only to fire a scheduled IR command
at your split AC — and re-checking NTP shortly before each event so the shot
lands on the right minute.

No cloud, no MQTT, no genset. Just: record the remote's buttons, set a weekly
schedule, sleep.

## How the clock stays accurate (the drift problem)

The ESP8266 runs the CPU from an accurate 26 MHz crystal **while awake**, but
during **deep sleep** it keeps time on a cheap internal RC oscillator that
drifts with temperature. Realistic figures:

| condition | typical drift |
|---|---|
| steady room temperature | ~1–3 % → **0.6–1.8 min per hour of sleep** |
| big temperature swings | up to ~5–7 % (3–4 min/hr) |

Drift only accumulates *during sleep*. Over a 6-hour sleep at 2 % that's ~7
minutes of error — enough to miss a minute-accurate schedule. Two mechanisms
keep it tight:

1. **Wake early by a margin.** Long dead-reckoned sleeps target *20 % early*
   (`DRIFT_MARGIN`), so even a slow RC clock arrives *before* the event, never
   after. 20 % is deliberately pessimistic — being early only costs a little
   battery, being late misses the command.
2. **Re-sync NTP right before each event.** Once we're within ~25 minutes
   (`NTP_LEAD_S`), the device briefly rejoins Wi-Fi and reads NTP again,
   erasing all accumulated drift. It then holds the last ~2 minutes awake on
   the accurate crystal (radio off) and fires exactly on the scheduled second.

So the "20 % inaccuracy" is absorbed twice: the early-wake margin guarantees we
never overshoot, and the fresh NTP sync corrects the clock to the second.

## Deep sleep vs. light sleep — why deep sleep here

Two ways to save power between events. This firmware uses **deep sleep**:

| | **Deep sleep** (this firmware) | Light sleep |
|---|---|---|
| Current (bare ESP-12) | **~20 µA** | ~1 mA (50× more) |
| Current (NodeMCU v3 board) | ~1–2 mA (board floor) | ~1–2 mA (board floor) |
| Keeps RAM / Wi-Fi / clock | No — full reboot on wake | Yes — resumes in place |
| Needs GPIO16→RST wire | **Yes** | No |
| Needs re-NTP after sleep | Yes (clock is lost) | No (clock survives) |
| Timekeeping accuracy | RC drift (corrected by NTP) | Crystal-accurate |
| Code complexity | Higher (RTC-memory state machine) | Lower |

**On a bare ESP-12, deep sleep wins by ~50×** and is the only way to reach
months on a battery — worth the GPIO16 wire and the re-NTP dance. On a stock
**NodeMCU v3 (LoLin)** the AMS1117 regulator + CP2102 USB chip leak ~1–2 mA no
matter what, so the two modes are nearly tied *on that board* — but the code is
written for deep sleep so the same firmware reaches full battery life on a bare
ESP-12 with no changes.

**Bottom line:** if you care about battery life, run this on a bare ESP-12 (or
a NodeMCU with the regulator/USB chip removed). On an unmodified NodeMCU v3
expect ~1–2 mA idle regardless.

## Wiring

```
GPIO16 (D0) ── RST        REQUIRED: the timer wake pulses RST through this wire.
                          Without it the device sleeps and never wakes.
IR LED  ── GPIO4 (D2) via 2N2222   (GPIO4 → ~220Ω → base, emitter → GND,
                                    collector → LED cathode, anode → +3V3 via ~100Ω)
VS1838B ── OUT → GPIO14 (D5), VCC 3V3, GND
LED     ── on-board GPIO2 (heartbeat after each IR send; optional)
```

## Life cycle

- **First power-on** (no Wi-Fi saved) or **pressing RST** → *Setup*: the AP
  `NtEremoteXX` (password `88888888`) comes up at `http://4.4.4.4` for **3
  minutes**. Enter home Wi-Fi, record ON/OFF/ECO, edit the schedule. Saving
  Wi-Fi immediately tests NTP so you see it sync. Then it sleeps.
- **Power-on with Wi-Fi already saved** (e.g. after a battery change) → silent
  recovery: no AP, just a quick STA + NTP, then straight back into the
  schedule.
- **Deep-sleep timer wake** → *Run*: Wi-Fi stays off until ~25 min before an
  event, then re-syncs NTP, holds, fires on time, and sleeps to the next event.

To reprogram, press **RST** to get the 3-minute AP back.

## Setup notes

- The home Wi-Fi **must be 2.4 GHz** (the ESP8266 has no 5 GHz radio).
- Wi-Fi credentials and the schedule live in LittleFS (survive power loss);
  the clock lives in RTC memory (survives deep sleep, **not** power loss —
  which is why power-on triggers a fresh NTP sync).
- Time zone is fixed to **Baghdad UTC+3** (`TZ_OFFSET_S`); change that constant
  for another zone. Schedules are entered in local wall-clock time.

## Tunable constants (top of `NtpEremote.ino`)

| constant | default | meaning |
|---|---|---|
| `AP_WINDOW_MS` | 180000 | setup/AP window = 3 min |
| `TZ_OFFSET_S` | 3×3600 | Baghdad UTC+3 |
| `DRIFT_MARGIN` | 0.20 | dead-reckoned sleeps wake this fraction early |
| `NTP_LEAD_S` | 1500 | re-sync NTP within this many sec of an event (25 min) |
| `HOLD_S` | 120 | within this many sec, stay awake and fire on the crystal |

## Files

- `NtpEremote.ino` — firmware
- `ntp_portal.h` — bilingual (Arabic-default, RTL) setup page served in the AP window
