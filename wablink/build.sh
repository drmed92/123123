#!/bin/sh
# Builds build/WABlink.apk without Gradle or the Google SDK manager.
# Needs (Ubuntu 24.04): apt install aapt dalvik-exchange zipalign apksigner android-sdk-platform-23 zip, plus a JDK.
set -eu
cd "$(dirname "$0")"

# API 34 framework jar (Maven Central) supplies android.*; the API 23 jar supplies java.* and links the manifest.
PLATFORM_JAR=/usr/lib/android-sdk/platforms/android-23/android.jar
ANDROID_JAR=.cache/android-all-34.jar
if [ ! -f "$ANDROID_JAR" ]; then
    mkdir -p .cache
    curl -fL -o "$ANDROID_JAR" https://repo1.maven.org/maven2/org/robolectric/android-all/14-robolectric-10818077/android-all-14-robolectric-10818077.jar
fi
echo "94b1490a891e9be559aa35c87cd8a0c163f32d83  $ANDROID_JAR" | sha1sum -c --quiet

rm -rf build
mkdir -p build/classes build/test

javac -d build/test src/app/wablink/BlinkState.java test/BlinkStateCheck.java
java -ea -cp build/test app.wablink.BlinkStateCheck

aapt package -f -M AndroidManifest.xml -I "$PLATFORM_JAR" -F build/app.unsigned.apk
javac -source 8 -target 8 -Xlint:-options -bootclasspath "$ANDROID_JAR:$PLATFORM_JAR" -d build/classes src/app/wablink/*.java
dalvik-exchange --dex --min-sdk-version=27 --output=build/classes.dex build/classes
(cd build && zip -q app.unsigned.apk classes.dex)
zipalign -f -p 4 build/app.unsigned.apk build/app.aligned.apk

# Keep this keystore: an update must be signed with the same key to install over the old version.
if [ ! -f wablink.keystore ]; then
    keytool -genkeypair -keystore wablink.keystore -storepass wablink -alias wablink \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=WA Blink"
fi
apksigner sign --ks wablink.keystore --ks-pass pass:wablink --out build/WABlink.apk build/app.aligned.apk
apksigner verify build/WABlink.apk
echo "Built $(pwd)/build/WABlink.apk"
