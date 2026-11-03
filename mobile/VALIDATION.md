# Validation on 2026-11-03

## Executed successfully on Windows

* Android debug APK build (game version 1.0.0, build number 2), with the normal
  com.guessthedrawing.mobile application ID. Gradle 9.6.0 / AGP 9.5.0.
* Eleven Kotlin unit tests: UTF-8 fragmentation, frame bounds, PBKDF2/HMAC
  compatibility, layer ordering, desktop curve geometry and hit-testing,
  canonical PCM frames, streaming 16/44.1/48 kHz resampling, all desktop palettes,
  moderation hierarchy and large duration arithmetic.
* Android lint: no errors. Remaining warnings cover pinned dependency versions,
  English-only strings, custom-view preview constructors and backup metadata.
* Real Qt service smoke test: registration, authentication, room listing,
  private-room challenge, drawing relay, late-join canvas snapshot, room chat,
  correct guess, round end and explicit leave.
* Native Android UI and game integration on the Pixel_10_Pro_XL emulator:
  login through the actual Activity, peer registration, private-room join,
  drawing synchronization, late spectating, chat and guessing.
* Touch curve endpoint/bend gestures, explicit finish/cancel, one-step undo
  and redo, and matching three-point curves received by another player and
  spectator through the real Qt server.
* Android AudioRecord capture, strict wire framing accepted and relayed by
  the Qt server, microphone stop/restart, and AudioTrack playback setup/cleanup.
  The test isolates audio focus between simulated players sharing one emulator.
* Search, full profile loading, country update, friend request acceptance and
  denial, accepted-notification clearing, blocking/unblocking, private messages,
  history/unread counts, local message deletion and room invitations.
* Staff data, server text/voice mute and unmute (including restored speech
  permission), role assignment/removal, temporary ban/unban, enforced removal,
  rejoin after unban, kick and rejoin after kick.
* Settled Ocean-theme emulator screenshots inspected for the game canvas,
  room chat/input, native controls, safe-area insets, and full player profile
  with flag/code, game stats, relationship and touch actions.
* Portable checks of Xcode source references, shared version configuration,
  plist/XML/asset metadata, fixtures and the Gradle wrapper.
* All Swift source files passed a tree-sitter syntax parse. This is not an
  Apple SDK type check or a substitute for an Xcode build.

The Qt servers used for the successful tests were freshly built from the
repository into an isolated test directory. Tests used temporary configs,
accounts, ports and a database, including a test-only master administrator.
The regression Android app uses a separate application ID and does not replace
the user's installed game. No production accounts, sanctions or data changed.

## Not executed here

* Swift compilation, the eight Swift core tests and the iOS simulator build
  require macOS / Xcode. The checked-in macOS CI job performs these checks when
  run; it was not run from this Windows session.
* Physical iPhone / Android device testing, microphone/speaker routing,
  background recovery on each device, release signing and store submission.

The emulator test verifies audio capture/framing/relay and playback code paths,
but does not establish human-audible sound quality, echo cancellation or physical
headset/Bluetooth routing. Those checks remain for real phones. See README.md
for controls, emulator connection setup and the remaining desktop feature scope.
