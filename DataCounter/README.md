# Data Counter (Android)

Shows how many MB of mobile data you have left, as a number icon in the status bar,
plus a home-screen widget.

- **App**: type the MB you have remaining and tap **Set**.
- **Status bar**: the number counts down as mobile data is used (checked every 10 s).
- **Widget**: shows the same number. **One tap resets it** to the value set in the app.

Build: open this folder in Android Studio and press Run, or `./gradlew assembleDebug`
→ `app/build/outputs/apk/debug/app-debug.apk`. Android 8.0+.

Notes: counts mobile data only (Wi-Fi is ignored); 1 MB = 1,000,000 bytes, like Android's
data-usage screen; survives reboots.
