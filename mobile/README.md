# Native mobile clients

Two independent apps connect directly to the existing Qt master and room servers:

* **Android:** Kotlin, native Android widgets, desktop-matched themes, a touch
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

For an Android emulator and servers running on this Windows computer, run:

~~~powershell
powershell -ExecutionPolicy Bypass -File mobile/tools/connect_emulator.ps1
~~~

Then enter **127.0.0.1** and **45450** in the app. The helper forwards both
the master port (45450) and room port (45451), so it also works when the master
only listens on localhost and the room advertises 127.0.0.1. Rerun it after
restarting the emulator. Use `-MasterPort` and `-RoomPort` for different ports;
forward each room port if you run multiple rooms. This does not start the servers.

Without forwarding, the Android emulator's host alias is 10.0.2.2; the master
must listen on an interface reachable through it and the advertised room host
must be reachable too. A LAN address will refuse connections when the master
only listens on localhost. On a physical phone, use the computer's LAN address
and the server settings above; 127.0.0.1 means the phone itself. Allow **Local
Network** access on iOS when prompted.

### Android emulator microphone

The emulator disables host microphone input by default, independently of the
app's microphone permission. The app can show **Mic on** and receive desktop
audio while sending silent samples. Enable the host input after each emulator
restart:

~~~powershell
powershell -ExecutionPolicy Bypass -File mobile/tools/enable_emulator_microphone.ps1
~~~

Then turn the game's Mic off and on. With multiple emulators, add
`-Serial emulator-5554` for the intended one. The connection helper also accepts
`-EnableMicrophone` to set up networking and microphone access together.

The equivalent emulator setting is **Extended Controls > Microphone > Enable
Host Microphone Access** (called **Virtual microphone uses host audio input**
in some versions). Windows must allow microphone access for the emulator.
See the [Android emulator microphone documentation](https://developer.android.com/studio/run/emulator-extended-controls).

Android shows a live input meter while Mic is on. After three seconds of entirely
silent samples, it displays microphone setup guidance. The indicator measures
captured sound, not just whether the recording device opened. It does not record
audio to disk or change the volume of transmitted speech. Pass `-Disable` to the
helper to turn emulator host input off again.

The existing protocol is plaintext TCP. Use a trusted private network or VPN;
these apps do not add TLS to the server. No server/session signing secrets are
included. Passwords, private-room passkeys and session tokens are not saved to
preferences.

## Build Android

Requirements: Android SDK 36 and network access for the first dependency
download. The checked-in wrapper uses Gradle 9.8.2 and AGP 9.5.0 with built-in
Kotlin. The existing daemon criteria request JetBrains JDK 21; Gradle can
provision it. Use JDK 21 for JAVA_HOME when building from a terminal.

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
variable. Store build numbers (Android versionCode 4 / iOS CURRENT_PROJECT_VERSION 3) are
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
  explicit permission, separate microphone/listening controls, and microphone
  shutdown in the background or when a round or moderation rule prohibits
  speaking. Android resamples supported hardware rates and manages audio focus;
  iOS converts the hardware format and recovers from audio-session changes.
  Headphones reduce feedback.
* Android document export and iOS sharing without broad storage permissions.

## Curves and appearance

Choose **Curve** and drag from one endpoint to the other. Drag the line to add
a bend, or drag a handle to adjust it. **Finish curve** commits it; **Cancel**
discards it. Adding a second bend completes the curve, matching the desktop
client's four-point limit. Undo removes a completed curve as one action. The
preview and network geometry use the same Catmull-Rom spline as the Qt client.

The theme menu offers **System** plus all 13 desktop palettes. The choice is
saved locally, applies to gameplay and community screens, and can change without
disconnecting. System follows the device appearance; the drawing canvas remains
white so artwork stays consistent across players.

## Languages and text direction

Both apps offer the desktop language list: English, Español, Français, Deutsch,
Português, Italiano, Русский, 中文（简体）, 日本語, 한국어, العربية and हिन्दी.
Use **Language** in the Android toolbar menu, or beside **Theme** on the iOS
login screen, profile screen and room menu. The first launch uses the device's
supported language; your selection is saved locally. Switching languages keeps
the current game connection. Android rebuilds its native screen and closes open
sheets, so switch before entering an unsaved form.

Arabic mirrors navigation, lists, forms and menus. Host addresses, usernames
and numeric connection fields use left-to-right input; the drawing canvas keeps
the desktop coordinate system. Embedded usernames, ranks, IP addresses and
numbers use Unicode directional isolation. Arabic and Hindi digits are accepted
in port/duration input. Dates use the selected locale. All other listed languages
use left-to-right layout.

The shared catalog covers gameplay, drawing tools, themes, profiles and ranks,
search, friends, inbox, notifications, moderation, voice guidance, common server
responses and iOS permission descriptions. User-written messages, room/layer
names, moderation reasons and the server's game words stay unchanged. Translating
the interface does not translate the shared game dictionary. Unknown server or
operating-system diagnostics are displayed as supplied. System-owned permission
and share dialogs follow the OS/app language settings.

`localization/desktop.json` is a snapshot of the 172 shared translations from
`src/client/main_window.cpp`; the UTF-8 pipe-separated `.tsv` files add mobile text.
`aliases.json` reuses equivalent labels; `compositions.json` combines labels in
statistic rows. `required.json` lists the required mobile keys. Native resources
and `catalog.json` are generated and checked in:

~~~sh
python mobile/tools/generate_localizations.py
python mobile/tools/generate_xcode_project.py
python mobile/tools/check_localizations.py
~~~

Generation fails on missing translations unless the explicit development-only
`--allow-incomplete` option is used. Android packages all languages even in an
App Bundle, allowing offline language changes. iOS uses localized `.lproj`
resources and explicit SwiftUI locale/layout direction. Keep placeholder numbers
unchanged when editing translations. See `localization/README.md` for details.

## Profiles, friends and moderation

Tap a player in the room, rankings, friends or search results to open their full
profile. It includes country flag/code, scores, ranks, wins/losses/abandoned
rounds, presence, room, relationship and server role. Long-press a player for
context actions; the same actions are available inside the profile.

**People & messages** provides:

* Player search by username or display name, friends, incoming/outgoing requests,
  accept/decline, remove friend, block/unblock, and room invitations.
* Private inbox conversations, unread counts, older-message loading, message
  deletion and conversation deletion. Deletion removes your own copy.
* In-app notifications and friend-request updates, including tap-to-clear
  accepted requests. These are live app notifications, not operating-system
  push notifications while the app is closed.
* Separate local text/voice mute controls and player reports where permitted.
* Staff moderation: kick, ban, text/voice sanctions, active ban/mute lists,
  unban/unmute and staff role assignment. Durations support seconds through
  years, a specific date/time, or permanent sanctions. Actions follow the
  desktop role hierarchy; the server remains authoritative about permissions.

Object scaling, clipboard actions and the desktop stroke-cutting eraser are not
included. The mobile eraser deletes whole objects. Polygon
draws a closed freehand gesture. Android provides a canvas color sampler;
iOS uses its system color picker.

Fonts and anti-aliasing differ by platform. Geometry and layer order use the
same normalized protocol coordinates; identical pixel rendering is not promised.
Long gestures stop at the server's 2,000-point object limit.

## Tests and maintenance

Android unit tests and ios/Tests cover fragmented UTF-8, coalesced frames, frame
limits, private-room proof compatibility, scene reductions, curve geometry,
voice framing, theme palettes and moderation roles. They share protocol/theme
fixtures. Android also checks resampling across audio input chunks.
The Android integration test drives native login against real Qt services and
checks social/inbox actions, profiles, curve gestures, undo/redo, microphone
capture and voice relay, moderation, drawing snapshots, chat and guessing.

~~~powershell
python mobile/tools/check_projects.py
python mobile/tools/check_localizations.py
python mobile/tools/server_smoke.py --bin build/Debug --qt-bin C:/Qt/6.9.1/msvc2022_64/bin
# With an already-running Android emulator:
python mobile/tools/server_smoke.py --bin build/Debug --qt-bin C:/Qt/6.9.1/msvc2022_64/bin --android
# Specify one emulator when several are running:
python mobile/tools/server_smoke.py --bin build/Debug --qt-bin C:/Qt/6.9.1/msvc2022_64/bin --android --serial emulator-5556
~~~

Use --version when the binaries use a release override. Tests use disposable
accounts, a private room, unique ports and a temporary database/config; they do
not use production data. Android mode uses a separate regression application ID,
seeds a staff account only in its disposable database, uses device-specific adb reverse and
removes its own forwarding rules afterward. Set ANDROID_HOME and JAVA_HOME.
After that test, run a normal assembleDebug to produce the installable app with
its standard application ID. See VALIDATION.md for executed checks and limits.

The Android localization test switches through all 12 languages, checks native
resources, RTL/LTR layout, unchanged user content, address input, numeric input,
and persistence after Activity recreation. The live-game test changes language
during a round before verifying drawing relay. Screenshots are written under
the test emulator's `Pictures/GTDLocalization` and `Pictures/GTDTest` folders.

The mobile GitHub Actions workflow builds/tests Android on Linux and builds
the iOS simulator app and runs Swift core tests on macOS. iOS compilation,
device microphone behavior and signing must be checked on Apple hardware;
a Windows build alone cannot validate them.

After adding Swift source files, regenerate the dependency-free Xcode project
with python mobile/tools/generate_xcode_project.py and review its diff. This
also regenerates the procedural pencil icon. Both apps package the existing
country-flags directory; country assets are not duplicated in the projects.

Theme colors live in fixtures/themes.json, copied from MainWindow::applyTheme.
Run python mobile/tools/generate_themes.py after changing those palettes to
regenerate the Kotlin/Swift tables and Android theme resources.
