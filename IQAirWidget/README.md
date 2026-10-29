# IQAir Widget (Android)

Home-screen widget showing IQAir air quality (US AQI), temperature (°C) and humidity for a city you choose.
The widget background takes the AQI colour. Built for the Samsung Galaxy S25 (works on Android 12+).

- **Sizes:** 1×1 (AQI, °C, humidity), 2×1 (adds category and city), 3×2 (adds main pollutant, wind, last update).
  All three can be resized; the layout adapts to the size.
- **Each widget shows** Outdoor (IQAir), Indoor (your sensor) or **Indoor vs outdoor** side by side. You choose when
  you place the widget; long-press → Settings changes it.
- **Tap the widget** to refresh now.
- **Updates** every 1–20 minutes (slider). Polling stops when no widget is placed and the lock screen option is off.
- **Lock screen / Always On Display:** an optional ongoing notification. Its status-bar icon is the AQI number itself,
  so the number shows in the status bar, on the lock screen and on AOD. The lock screen card is coloured by AQI.

## Setup

1. Install `app-release.apk` (allow "Install unknown apps" for your browser or file manager).
2. Get a free API key: <https://dashboard.iqair.com> → Air quality API → Create key.
3. Open **IQAir Widget**, paste the key, tap **Choose city** (country → state → city), pick an interval, tap **Save & update now**.
4. Long-press the home screen → Widgets → IQAir Widget → choose a size, then choose what it shows.

### Indoor sensor (ThingSpeak)

Enter the channel ID (e.g. `3427507`) under **Indoor sensor**. The channel must be public, and its fields must be named
`pm2.5`, `pm10`, `co2`, `temp` and `hum` (any order; `PM2.5`/`Temperature`/`Humidity` also work).

- **AQI:** PM2.5 and PM10 (µg/m³) become US AQI with the EPA formula and the 2024 breakpoints, which IQAir also uses.
  The higher of the two is shown, so indoor and outdoor numbers and colours are comparable.
- **CO₂, temperature and humidity** have no AQI and are shown as measured.
- The EPA AQI is defined on hourly/daily averages. The widget converts the latest reading, so cooking or
  cleaning shows up right away as a spike.
- The PMS7003 is an uncorrected low-cost sensor and can read higher than reference monitors, especially in humid air.
- If the channel has had no new entry for 30 minutes, the widget keeps the last values and shows "⚠ indoor: sensor silent".
- Only the sources a widget (or the lock screen notification) shows are polled, so an indoor-only widget uses no IQAir quota.

### Free key limits

The free Community key allows **5 calls/min, 500/day, 10,000/month**. 1 minute = 1,440 calls/day, so the key runs out
in about 8 hours. **5 minutes or slower** stays within the limits. IQAir stations report about once an hour, so faster
polling rarely shows a new number.

### Samsung settings for the lock screen and AOD

- Turn on **Show AQI on lock screen, Always On Display and status bar** in the app and allow notifications.
- Settings → Notifications → Lock screen notifications: on, show content.
- Always On Display: enable notification icons. AOD can only show icons, so you get the number, not the colour.
- Settings → Notifications → Status bar: "All notifications" or "3 most recent", so the number is not hidden.
- Tap **Allow background updates (battery)** in the app, or set Settings → Apps → IQAir Widget → Battery →
  **Unrestricted**. Otherwise One UI may put the app to sleep and stop updates.
- The widget declares lock screen support. One UI only offers it where Samsung allows third-party lock screen widgets.
  Good Lock → LockStar can place any home-screen widget on the lock screen.

## Build

```sh
export ANDROID_HOME=/path/to/android-sdk   # platform 35 + build-tools
./gradlew test assembleRelease             # → app/build/outputs/apk/release/app-release.apk
```

The release APK is signed with the local debug key for sideloading. An APK built on another machine has a different
signature, so uninstall the old app before installing it.
