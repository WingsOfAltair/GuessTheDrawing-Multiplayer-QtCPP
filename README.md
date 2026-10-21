# Guess The Drawing

Guess The Drawing is a Qt 6/C++20 multiplayer drawing-and-guessing game. It consists of three programs:

- **Desktop client (`gtd_client`)** — account sign-in, room browser, drawing canvas, chat, voice, profile, and moderation controls.
- **Master service (`gtd_master`)** — SQLite account/profile storage, room directory, authentication, server registration, and moderation authorization.
- **Dedicated room server (`gtd_game_server`)** — hosts a room and relays game, chat, and voice traffic while enforcing master-authorized joins and sanctions.

The network protocol uses newline-delimited JSON. The project does not provide TLS; deploy only on trusted networks or behind a TLS-terminating proxy.

## Requirements

- CMake 3.21 or newer
- C++20 compiler supported by Qt (Visual Studio/MSVC on Windows, or a compatible GCC/Clang toolchain)
- Qt 6.4 or newer with Core, Multimedia, Network, Sql, and Widgets
- Qt SQLite SQL driver plugin at runtime

## Build

### Windows / Visual Studio

Open the repository folder in Visual Studio as a CMake project, or use a PowerShell/developer terminal. Point CMake to the Qt kit matching the compiler:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64"
cmake --build build --config Debug --parallel 4
```

For a Release build, replace `Debug` with `Release`. On the IDE-configured workspace, use the Qt MSVC kit corresponding to the selected Visual Studio toolset.

### Linux

Install CMake, a C++20 compiler, and Qt development packages for Core, Multimedia, Network, Sql, Widgets, and Test. Then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Use `-DCMAKE_BUILD_TYPE=Release` for Release builds.

## Tests

CTest targets cover account storage, game rules, protocol parsing, canvas models and widgets, game-server behavior, master authorization, and configuration validation.

Windows multi-configuration build:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Linux single-configuration build:

```sh
ctest --test-dir build --output-on-failure
```

To run an individual CTest target, pass `-R` with its name, for example `ctest --test-dir build -C Debug -R gtd_master_tests --output-on-failure`.

## First run and local setup

Each executable reads `gtd-config.json` from its application directory by default. The first launch creates it with defaults and persistent random server/session secrets. The config file is merged with defaults on subsequent launches, so newly introduced values are added to older config files. A malformed or unsupported config is rejected without overwriting it.

All three programs must use matching server/session secrets and compatible master endpoints. If the executables are in different output folders, pass the same config file to each with `--config <path>`.

To run locally on Windows, set Qt's runtime paths and launch each process in a separate PowerShell terminal:

```powershell
$qt = "C:\Qt\6.9.1\msvc2022_64"
$env:PATH = "$qt\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$qt\plugins"
& ".\build\Debug\gtd_master.exe" --config ".\gtd-config.json"
& ".\build\Debug\gtd_game_server.exe" --config ".\gtd-config.json"
& ".\build\Debug\gtd_client.exe" --config ".\gtd-config.json"
```

On Linux, run the programs in separate terminals with the same config path:

```sh
./build/gtd_master --config ./gtd-config.json
./build/gtd_game_server --config ./gtd-config.json
./build/gtd_client --config ./gtd-config.json
```

Create an account in the client, then select the registered room to join. To host more rooms, run additional game-server processes with unique server IDs, room IDs, and listening ports. Use `--help` on each executable for its supported command-line options.

## Configuration reference

The following values are in the generated `gtd-config.json`. JSON keys and value types are case-sensitive. Addresses and paths are strings; port and duration values are JSON numbers.

| Section | Key | Default | Accepted values / meaning |
|---|---|---:|---|
| `version` | — | `1` | Configuration format version. Only version `1` is supported. |
| `security` | `serverSecret` | generated | Persistent string of at least 32 characters. Shared registration secret used between master and game servers. |
| `security` | `sessionSecret` | generated | Persistent string of at least 32 characters. Shared signing secret used to verify account session tokens. |
| `master` | `address` | `"127.0.0.1"` | Non-empty bind address accepted by `QHostAddress`; use a reachable interface for remote services. |
| `master` | `port` | `45450` | TCP port, integer `1`–`65535`. |
| `master` | `database` | `"gtd.sqlite"` | Non-empty SQLite database path. Relative paths resolve from the config file's directory. |
| `master` | `superAdminRefreshSeconds` | `1` | `1`–`3600` seconds. How often the running master detects direct SQL changes to the super-admin table and updates connected game-server role metadata. Authentication and authorization requests read current database status. |
| `gameServer` | `serverId` | `"local-room-1"` | Non-empty unique ID for this dedicated server. Use letters, digits, `_`, or `-`; maximum 64 characters. |
| `gameServer` | `roomId` | `"room-1"` | Non-empty room identifier. |
| `gameServer` | `roomName` | `"Drawing Room"` | Non-empty room display name. |
| `gameServer` | `publicHost` | `"127.0.0.1"` | Non-empty host/address advertised to clients; set this to a reachable address when hosting remotely. |
| `gameServer` | `port` | `45451` | TCP listen port, integer `0`–`65535`. `0` asks the operating system for an ephemeral port. |
| `gameServer` | `masterAddress` | `"127.0.0.1"` | Non-empty host/address of the master service. |
| `gameServer` | `masterPort` | `45450` | Master TCP port, integer `1`–`65535`. |
| `gameServer` | `roundSeconds` | `60` | Drawing round duration, integer `10`–`600` seconds. |
| `gameServer` | `intermissionSeconds` | `15` | Delay before a round starts, integer `0`–`120` seconds. `0` disables the intermission. |
| `gameServer` | `maxPlayers` | `8` | Maximum active players in the room, integer `2`–`32`; spectators may join a full room. |
| `client` | `masterAddress` | `"127.0.0.1"` | Non-empty host/address of the master service. |
| `client` | `masterPort` | `45450` | Master TCP port, integer `1`–`65535`. |
| `client` | `language` | `"en"` | Client UI language code. Available UI choices: `en`, `es`, `fr`, `de`, `pt`, `it`, `ru`, `zh`, `ja`, `ko`, `ar`, `hi`. |
| `client` | `theme` | `"system"` | Client theme: `light`, `dark`, `system`, `ocean`, `forest`, `sunset`, `purple`, `solarized`, `high-contrast`, `rose`, `midnight`, `nord`, `dracula`, or `monokai`. |
| `client` | `exportDirectory` | `"Saved/Drawings"` under the app directory | Non-empty path where client drawing exports are saved. Relative paths resolve from the config directory. |
| `client` | `dictionaryPath` | `"dictionary"` under the app directory | Non-empty path used by the game server for guess words. The default directory is bundled/copied next to the game-server executable. |

Secrets should remain private and be identical for the processes that need to verify each other's registrations or tokens. Environment variables `GTD_SERVER_SECRET` and `GTD_SESSION_SECRET` override the respective JSON secrets for the master and game-server executables. Command-line options take precedence over JSON values for options that executable exposes.

### Command-line overrides

All programs accept `--config <path>`.

- **Client:** `--master-address <host>`, `--master-port <port>`.
- **Master:** `--address <address>`, `--port <port>`, `--database <path>`, `--server-secret <secret>`, `--session-secret <secret>`.
- **Game server:** `--server-id <id>`, `--room-id <id>`, `--room-name <name>`, `--public-host <host>`, `--port <port>`, `--master-address <host>`, `--master-port <port>`, `--round-seconds <seconds>`, `--intermission-seconds <seconds>`, `--max-players <count>`, `--server-secret <secret>`, `--session-secret <secret>`.

Command-line ports, room duration/intermission, player limits, and secret lengths are checked at startup. Run `<executable> --help` for option descriptions.

## Gameplay and canvas

Only the current drawer can edit the shared canvas. Tools include select/move/scale, pen, brush, airbrush, curve, polygon, stroke eraser, object eraser, rectangle, rounded rectangle, ellipse, line, fill, color picker, and text. The layer panel supports selecting, adding, deleting, renaming, visibility, locking, and reordering. Canvas changes synchronize to room participants, including late joiners. During a round, the drawer can listen to room voice but cannot send room chat or microphone audio.

## Server moderation

Moderation and staff roles are scoped to a dedicated game server ID. A server admin can grant/revoke moderators. Admins and moderators can kick, ban, and mute ordinary players. Only master super-admins can assign/revoke server admins and moderate staff at any level. Regular players can mute another ordinary player's text and/or voice locally; this setting applies only to their own playback/chat view. Staff and master super-admins are protected from regular-player mutes.

Kicks disconnect immediately. Bans block future joins. Server text and voice mutes are independently selectable and enforced by the game server. Ban/mute sanctions can be permanent, expire after minutes/hours/days, or end at a selected local date/time (stored as UTC). Active staff can open the Moderation screen to view bans, mutes, and staff, view profiles, revoke sanctions, and manage roles where authorized. Roster entries and profiles display server staff and master super-admin status.

### Granting the first master super-admin

Super-admin status is stored in the master's SQLite database and is never accepted from client profile claims. Create the account in the client, then insert its normalized username into the database:

```sql
INSERT INTO master_super_admins(username) VALUES ('account_username');
```

The running master detects the change according to `master.superAdminRefreshSeconds` (default one second); a restart is not required. Restrict direct database access to trusted operators and back up the database.

## Security and deployment notes

The generated config contains authentication/registration secrets; the SQLite database contains accounts and moderation state. Protect both and do not commit or distribute production copies. Change loopback endpoints to reachable addresses only as required, configure host firewalls, and use trusted private networks or a TLS-terminating proxy for network deployments. The project does not currently implement TLS, profanity filtering, player reporting, or automatic content scanning.
