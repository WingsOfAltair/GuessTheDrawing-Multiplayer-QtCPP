# Guess The Drawing

A Qt 6/C++20 multiplayer drawing-and-guessing game with three executables: a Qt desktop client, a central master service, and dedicated game servers. The master owns account/profile storage and lists registered rooms; game servers host independent rooms and relay game events.

## Requirements

- Qt 6.4 or newer with Core, Multimedia, Network, Sql, and Widgets modules
- CMake 3.21 or newer
- A C++20 compiler supported by Qt

## Build

The project builds on Windows and Linux with CMake and a Qt 6.4+ kit. On Windows, open this folder in Visual Studio as a CMake project, or configure it from a developer shell:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use the Qt installation path that matches the compiler/toolset used to build the project. For example, the MSVC Qt kit must be used with the Visual Studio compiler.

On Linux, install CMake, a C++20 compiler, and Qt 6.4+ development packages for Core, Network, Sql, Widgets, and Test. Ensure the Qt SQLite SQL driver plugin is installed at runtime. Configure and build with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Local development

On first launch, each executable creates `gtd-config.json` beside itself if it is missing. The generated file contains default endpoints and two independent persistent secrets shared by the master and game servers. The SQLite database path is relative to the config file. If executables are in different folders, pass the same config path to each with `--config`.

On Windows, configure Qt's runtime paths and start the master, game server, and client in separate terminals:

```powershell
$qt = "C:\Qt\6.9.1\msvc2022_64"
$env:PATH = "$qt\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$qt\plugins"
& ".\build\Debug\gtd_master.exe"
& ".\build\Debug\gtd_game_server.exe"
& ".\build\Debug\gtd_client.exe"
```

On Linux, run the executables from the build directory in separate terminals so they share the generated configuration file:

```sh
./build/gtd_master
./build/gtd_game_server
./build/gtd_client
```

Use `--config <path>` to select a shared file explicitly. Config values supply defaults; command-line options override them, and `GTD_SERVER_SECRET`/`GTD_SESSION_SECRET` override the corresponding generated secrets. A malformed config is reported and left unchanged. Treat the config as sensitive: it contains signing secrets. The generated config and database are excluded from source control. Use `--help` on each executable to see available overrides.

Create an account in the client, select the registered room, and join. To host additional rooms, run more game-server processes with unique server IDs, room IDs, and ports. The room timer is configurable per server with `--round-seconds` (10-600 seconds). Use `--help` on each executable for all options. Run tests with `ctest --test-dir build -C Debug --output-on-failure`.

## Canvas editing

Only the current drawer can edit the shared canvas. Use the tool selector for Select/move/scale, Pen, Eraser (cuts freehand strokes into separate surviving segments), Object Eraser (deletes a whole object), Rectangle, Ellipse, Line, or Text. Select an object on the canvas to move it; drag one of its handles to resize it. The color picker, brush/eraser size, text size, and Fill shapes option control new objects. The layer panel supports selecting, adding, renaming, hiding/showing, locking/unlocking, reordering, and deleting layers. The stroke eraser radius follows the brush/eraser size control; it does not affect shapes or text. Canvas changes synchronize to everyone in the room, including players joining during a round.

Network messages use newline-delimited, versioned JSON. Configure non-loopback endpoints and strong secrets before exposing services to a network. This project is an initial implementation and does not provide TLS; use a trusted network or a TLS-terminating proxy for deployments.
