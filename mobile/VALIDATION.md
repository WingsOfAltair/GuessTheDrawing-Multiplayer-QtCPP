# Validation on 2026-11-03

## Executed successfully on Windows

* Android debug APK build (game version 1.0.0).
* Six Kotlin protocol/scene unit tests, including UTF-8 fragmentation, frame
  bounds, PBKDF2/HMAC compatibility and layer ordering.
* Android lint: no errors. Remaining warnings cover pinned dependency versions,
  English-only strings, custom-view preview constructors and backup metadata.
* Real Qt service smoke test: registration, authentication, room listing,
  private-room challenge, drawing relay, late-join canvas snapshot, room chat,
  correct guess, round end and explicit leave.
* Native Android UI and game integration on the Pixel_10_Pro_XL emulator:
  account creation through the actual Activity, private-room join, drawing
  synchronization between clients, late spectating, chat and guessing.
* Emulator screenshot inspected for the game canvas, native controls,
  safe-area insets, room chat and input layout.
* Portable checks of Xcode source references, shared version configuration,
  plist/XML/asset metadata, fixtures and the Gradle wrapper.

The Qt servers used for the successful tests were freshly built from the
repository into an isolated test directory. Tests used temporary configs,
accounts, ports and a database. Existing Debug binaries were inconsistent:
the master reported 1.0.1 and the room server 1.0.0. Rebuild both with the same
version before using those binaries with any client.

## Not executed here

* Swift compilation, Swift tests and the iOS simulator build require macOS /
  Xcode. The checked-in macOS CI job performs these checks when run.
* Physical iPhone / Android device testing, microphone/speaker routing,
  background recovery on each device, release signing and store submission.

The source projects include voice support, but emulator gameplay verification
did not test live microphone audio. See README.md for the precise feature scope
and desktop features that are not included.
