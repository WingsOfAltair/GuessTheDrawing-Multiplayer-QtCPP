# Native mobile clients

Two independent apps connect directly to the existing Qt master and room servers:

* **Android:** Kotlin, native Android widgets, Material system styling, a touch
  canvas, Android microphone permission flow, and the system document picker.
  Open **android/** in Android Studio.
* **iPhone / iPad:** Swift, SwiftUI navigation, forms, lists, menus, color picker
  and share sheet, with a UIKit touch / Apple Pencil canvas.
  Open **ios/GuessTheDrawing.xcodeproj** in Xcode on a Mac.

Neither app embeds Qt, a browser, Flutter, or a desktop window.

## Play

1. Run the master and a room server with **the same application version**.
2. Configure master.address to a reachable interface (for example 0.0.0.0) and
   gameServer.publicHost to the host's LAN IP. The advertised room address must
   be reachable from the phone as well as the master address.
3. Connect the phone and host to the same network, allow the configured TCP ports
   through the host firewall, and enter the master address and port in the app.
4. Sign in or create an account, select **Play** or **Watch**, and enter a passkey
   for a private room. Rounds start automatically when enough players join.

On an Android emulator the host loopback alias is 10.0.2.2. A room must also
advertise 10.0.2.2 for that emulator, or a LAN address reachable by all clients.
On a physical phone, 127.0.0.1 means the phone itself. Allow **Local Network**
access on iOS when prompted.

The existing protocol is plaintext TCP. Use a trusted private network or VPN;
these apps do not add TLS to the server. No server/session signing secrets are
included. Passwords, private-room passkeys and session tokens are not saved to
preferences.

## Build Android

Requirements: Android SDK 36, JDK 17 or 21, and network access for the first
dependency download. The checked-in wrapper uses Gradle 8.14, AGP 8.11.1 and
Kotlin 2.2.0. JDK 25 is not supported by this pinned Gradle version.

~~~powershell
cd mobile/android
./gradlew.bat assembleDebug testDebugUnitTest lintDebug
~~~

On macOS/Linux use bash ./gradlew instead. Android Studio configures the SDK
path, or set ANDROID_HOME. local.properties is deliberately untracked.
The APK is app/build/outputs/apk/debug/app-debug.apk.
Minimum device API is 26 (Android 8); target/compile API is 36.

For release distribution, configure your own Android signing key in Android
Studio. Signing keys and store credentials are not included.

## Build iOS

Requirements: a Mac, Xcode 15 or newer with an iOS SDK, and an iOS 16+ device or
simulator. There are no CocoaPods or third-party Swift package dependencies.
The Xcode project and shared scheme are checked in.

~~~sh
swift test --package-path mobile/ios
xcodebuild -project mobile/ios/GuessTheDrawing.xcodeproj \
  -scheme GuessTheDrawing -configuration Debug \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath mobile/ios/build CODE_SIGNING_ALLOWED=NO build
~~~

For a physical device, select your Apple development team in Signing &
Capabilities and a unique bundle ID if necessary. Select the device and Run.
Archive and sign with your own team for TestFlight / App Store.
Microphone permission is requested only when the player enables voice.

## Exact game-version matching

GameVersion.xcconfig is the shared default (1.0.0, matching the root CMake
project). Android reads this file; Xcode imports it. Update it alongside the
server version for a release, or override it for a particular build:

~~~powershell
# Android for a server built as 1.0.1
./gradlew.bat assembleDebug -PgtdVersion=1.0.1 -PgtdVersionCode=2
~~~

~~~sh
xcodebuild -project mobile/ios/GuessTheDrawing.xcodeproj \
  -scheme GuessTheDrawing -sdk iphonesimulator \
  CODE_SIGNING_ALLOWED=NO GTD_GAME_VERSION=1.0.1 CURRENT_PROJECT_VERSION=2 build
~~~

Android also accepts GTD_RELEASE_VERSION, matching the Qt build environment
variable. Store build numbers (versionCode / CURRENT_PROJECT_VERSION) are
independent of the exact protocol app-version string. Mismatches are shown in
the app; mobile builds never download or execute the Windows updater.

## Included gameplay

* Registration / login, room browser, private-room PBKDF2-HMAC challenge,
  profile edits / password changes, country flags and two leaderboards.
* Player and spectator joins, player queue / role switching, reconnects within
  the server grace period, countdowns, private word assignment, guessing,
  scores, roster, room chat and moderation notices.
* Live touch pen, brush, airbrush, curve, polygon, line, rectangle, rounded
  rectangle, ellipse, move, object eraser, flood fill and text; undo/redo;
  layer creation, selection, renaming, ordering, visibility, locking and deletion.
* Rendering of all current desktop object types, including PNG fill objects,
  with full scene snapshots for late joiners.
* Optional desktop-compatible 16 kHz mono PCM voice; bounded playback buffers,
  explicit permission, and microphone shutdown in the background or when a
  round prohibits speaking. Headphones reduce feedback.
* Android document export and iOS sharing without broad storage permissions.

This is a native gameplay client, not full desktop feature parity. Desktop
social / inbox / invitations, staff administration screens, translations,
desktop themes, object scaling, clipboard actions and stroke-cutting eraser
are not included. The mobile eraser deletes whole objects. Curve and polygon
tools trace a gesture without desktop control-point editing. Android provides
a canvas color sampler; iOS uses its system color picker.

Fonts and anti-aliasing differ by platform. Geometry and layer order use the
same normalized protocol coordinates; identical pixel rendering is not promised.
Long gestures stop at the server's 2,000-point object limit.

## Tests and maintenance

Android unit tests and ios/Tests cover fragmented UTF-8, coalesced frames, frame
limits, private-room proof compatibility and scene reductions. Both consume
fixtures/protocol.json, generated with Python's standard crypto primitives.
The Android integration test drives native login and plays a round using real
Qt services, including drawing relay and a late spectator.

~~~powershell
python mobile/tools/check_projects.py
python mobile/tools/server_smoke.py --bin build/Debug --qt-bin C:/Qt/6.9.1/msvc2022_64/bin
# With an already-running Android emulator:
python mobile/tools/server_smoke.py --bin build/Debug --qt-bin C:/Qt/6.9.1/msvc2022_64/bin --android
~~~

Use --version when the binaries use a release override. Tests use disposable
accounts, a private room, unique ports and a temporary database/config; they do
not use production data. Android mode uses adb -e reverse and removes its own
forwarding rules afterward. Set ANDROID_HOME and a compatible JAVA_HOME.

The mobile GitHub Actions workflow builds/tests Android on Linux and builds
the iOS simulator app and runs Swift core tests on macOS. iOS compilation,
device microphone behavior and signing must be checked on Apple hardware;
a Windows build alone cannot validate them.

After adding Swift source files, regenerate the dependency-free Xcode project
with python mobile/tools/generate_xcode_project.py and review its diff. This
also regenerates the procedural pencil icon. Both apps package the existing
country-flags directory; country assets are not duplicated in the projects.
