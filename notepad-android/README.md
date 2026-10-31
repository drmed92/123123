# Classic Notepad (Android)

Free, ad-free notepad with a Windows XP-style caption bar (✕ to close) and a File / Edit / View / Help menu bar.
No permissions at all — not even internet — so it cannot show ads or send data anywhere.

**Try it:** install [`dist/ClassicNotepad-1.0-debug.apk`](dist/ClassicNotepad-1.0-debug.apk) (Android 8.0+; allow "install unknown apps").

| Feature | Where |
|---|---|
| Save to the app's private storage (default) | File > Save / Save As… > *Save in App* |
| Save anywhere, any extension | File > Save As… > *Browse…* (pick a type or type your own extension) |
| Open | File > Open… (any file) or File > Open from App Storage… (long-press to delete) |
| Word wrap, zoom | View |
| Large files (> 1 MB) | Open read-only, 256 KB per page, ◄ ► buttons at the bottom |
| Live log following (`tail -f`) | Open a file, then View > Follow Log |
| Bold / Italic / Underline / Color | Select text: **B I U A** appear in the selection bar. Kept when saved as `.html` |
| Unsaved text survives closing/swiping away | Restored on next launch; ✕ asks to save |

## Build

```
./gradlew assembleDebug        # APK -> app/build/outputs/apk/debug/
./gradlew testDebugUnitTest    # JVM + Robolectric tests
```

## Before publishing on Google Play

1. Pick your final `applicationId` in `app/build.gradle` (it can never change after the first upload).
2. Create a release keystore (`keytool -genkey -v -keystore release.jks -keyalg RSA -keysize 2048 -validity 10000 -alias notepad`), keep it out of git, add a `signingConfigs.release` block.
3. `./gradlew bundleRelease` and upload the `.aab`. Play needs a privacy policy URL; "collects no data" is accurate here.
