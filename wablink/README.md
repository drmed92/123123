# WA Blink

While a WhatsApp, SMS, missed call or Gmail notification is unread and the screen is off, a dim black
screen opens over the lock screen. Its clock and small top-left icons blink (default 2 s on, 4 s off):
WhatsApp green, SMS blue, missed call orange, Gmail red. Tap it, or press power, to silence it
until the next message or your next unlock. It closes by itself once everything is read.

Brightness and the on/off times are sliders on the app's main screen.

Android does not let apps change the system Always-On Display, so this draws its own AOD-style screen instead.

## Install

1. Copy `WABlink.apk` to the phone and open it (allow "install unknown apps" when asked). It installs over
   an earlier version as long as both were signed with the same keystore.
2. Open **WA Blink** and tap each button until it shows ✓:
   - Notification access
   - Allow notifications
   - Full-screen alerts (Android 14+)
3. Tap **Test blink** to see it.
4. Optional, if it stops working after a while: Settings > Apps > WA Blink > Battery > Unrestricted.

## Build

`./build.sh` (Ubuntu 24.04, no Gradle or Google SDK manager needed; dependencies are listed at the top of the script).
