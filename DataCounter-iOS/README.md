# Data Counter (iPhone)

Shows how many MB of cellular data you have left. Made for iOS 17+, including iPhone 17 Pro Max.

- **Top of the screen**: the number sits in the **Dynamic Island** (and on the Lock Screen) as a Live Activity.
  iOS does not let apps add icons to the real status bar; this is the closest thing Apple allows.
- **App**: type the MB you have remaining and tap **Set**.
- **Widget**: shows the same number. **One tap resets it** to the value set in the app.

## Install (needs a Mac with Xcode 26)

1. `brew install xcodegen`, then run `xcodegen` in this folder. It creates `DataCounter.xcodeproj`.
2. Open it. For both the **DataCounter** and **DataCounterWidget** targets, open
   *Signing & Capabilities* and choose your Apple ID as the Team.
3. Plug in your iPhone (turn on *Settings > Privacy & Security > Developer Mode*), pick it at the top, press Run.
4. Open the app, set your MB, allow Live Activities, then add the **Data left** widget to your Home Screen.

If Xcode says the bundle ID or App Group is taken, change `com.drmed.datacounter` /
`group.com.drmed.datacounter` in `project.yml` and `Shared/DataCounter.swift`, then run `xcodegen` again.

## iOS limits (Apple's rules, not fixable in the app)

- **Not live like Android.** iOS doesn't let apps run all the time. The number updates when you open the app,
  tap the widget, or iOS gives the app a background refresh (iOS picks when: often every 15 min to a few hours,
  and never if you swipe the app away in the app switcher). The widget refreshes about every 15–30 min.
- **The Dynamic Island counter ends after 8 hours** (iOS rule). Opening the app brings it back
  (tapping the widget does too, but that also resets the count).
- **Free Apple ID**: the app stops opening after 7 days; press Run in Xcode again to renew.
- Counts cellular data only (Wi-Fi is ignored); 1 MB = 1,000,000 bytes.
