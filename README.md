# Basilisk

[![CI](https://github.com/austinmhorn/basilisk/actions/workflows/ci.yml/badge.svg?branch=dev)](https://github.com/austinmhorn/basilisk/actions/workflows/ci.yml)

**Basilisk** is a pre-alpha deterministic C++ cave-hunting game engine designed to support native desktop, browser/WebAssembly, and authoritative multiplayer server clients from one shared core.

## Versioning

Game development and simulator-bot development use separate version lines:

- **Game:** pre-alpha `v0.x`
- **Simulation benchmark:** **BOT V3.14** is the current frozen balance/regression baseline.

`BOT V*` labels refer only to simulator strategy/telemetry iterations; they are not game versions.

See [`docs/CORE_CLIENT_BOUNDARY_AUDIT.md`](docs/CORE_CLIENT_BOUNDARY_AUDIT.md) for the current transition plan from simulation-first development to a playable client.

## Client architecture

`BasiliskCore` is the shared rules and player-snapshot layer. The C++ SDL3
client in `clients/game` links that same Core target for both supported
graphical platforms:

```text
BasiliskCore
    |
clients/game (C++ / SDL3)
    |
    +-- native desktop
    +-- Emscripten / WebAssembly browser
```

`clients/game` is currently a minimal window/render-loop foundation, not a
gameplay interface. `clients/web-debug` remains a separate developer debug and
acceptance harness; it is not the future shipping browser client.

## Development

The active development branch is `dev`.

### Install native development dependencies

Basilisk currently requires Git, CMake 3.25 or newer, a C++20 toolchain, and
Python 3 for the visual debug bridge. The graphical client uses SDL3 3.4.10,
which CMake downloads from the official release archive and verifies with a
pinned SHA-256 hash when `BASILISK_BUILD_GAME` is enabled. Do not install SDL3
globally. Run the bootstrap script for your operating system from the
repository root; each script is safe to rerun.

**macOS** (Homebrew and Apple Clang from Xcode Command Line Tools):

```bash
./scripts/install-dependencies-macos.sh
```

The script does not install Homebrew automatically. If Homebrew is absent, it
stops with a link to the official installer. If Xcode Command Line Tools are
absent, it starts `xcode-select --install`; finish that installation and rerun
the script.

**Ubuntu** (`apt`, GCC via `build-essential`):

```bash
./scripts/install-dependencies-ubuntu.sh
```

The script uses `sudo` when the current user is not root.
It also installs the X11/OpenGL development headers needed to compile SDL's
desktop video support; SDL itself still comes from the project CMake build.

**Windows** (PowerShell, `winget`, and Visual Studio 2022 Build Tools/MSVC):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-dependencies-windows.ps1
```

This one-command process bypasses execution policy only for that invocation; it
does not change the global policy. The installer adds the Visual Studio C++
workload and recommended Windows SDK. Open a new PowerShell terminal afterward
if newly installed commands are not visible. No Visual Studio-specific CMake
generator is required; CMake selects an installed default generator.

### Install the optional browser toolchain

Emscripten is required only for WebAssembly development. Native contributors
can skip this section. CI and the reproducible developer instructions below
pin Emscripten `6.0.4`.

On macOS, the locally proven Homebrew route is:

```bash
./scripts/install-dependencies-macos.sh --with-web
```

That option installs Homebrew's `emscripten` formula in addition to the native
tools. Equivalently, run `brew install emscripten`. Homebrew controls the exact
formula version, so use the official SDK route below when matching CI exactly.

On Ubuntu or macOS, install and activate the pinned official Emscripten SDK in
a sibling directory:

```bash
git clone --depth 1 --branch 6.0.4 https://github.com/emscripten-core/emsdk.git ../emsdk
../emsdk/emsdk install 6.0.4
../emsdk/emsdk activate 6.0.4
source ../emsdk/emsdk_env.sh
```

On Windows PowerShell:

```powershell
git clone --depth 1 --branch 6.0.4 https://github.com/emscripten-core/emsdk.git ..\emsdk
..\emsdk\emsdk.bat install 6.0.4
..\emsdk\emsdk.bat activate 6.0.4
. ..\emsdk\emsdk_env.ps1
```

Activate `emsdk_env.sh` or `emsdk_env.ps1` again in each new shell before a
web build. These SDK commands install their own compiler and supporting tools;
they do not install SDL globally.

### Build and test

The default build does not fetch or build SDL:

```bash
git switch dev
git pull origin dev
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The common commands also work in PowerShell when using a single-configuration
generator.

With CMake's default Visual Studio multi-configuration generator on Windows,
select the same configuration for building and testing:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

### Build the native graphical foundation

Enable the opt-in SDL3 target in a separate build directory:

```bash
cmake -S . -B build-game -DBASILISK_BUILD_GAME=ON
cmake --build build-game
```

The executable is `build-game/clients/game/BasiliskGame` on macOS/Linux. With
a Visual Studio multi-configuration generator it is normally
`build-game\clients\game\Debug\BasiliskGame.exe`, unless another configuration
was selected. The current client opens a window and renders a simple frame; it
does not implement gameplay UI yet.

### Build the browser graphical foundation

After activating Emscripten, configure and build the same `clients/game`
source through its CMake toolchain:

```bash
emcmake cmake -S . -B build-web \
  -DBASILISK_BUILD_GAME=ON \
  -DBASILISK_BUILD_CLI=OFF \
  -DBASILISK_BUILD_SIM=OFF \
  -DBASILISK_BUILD_TESTS=OFF
cmake --build build-web
```

The browser artifacts are generated under `build-web/clients/game/` as
`BasiliskGame.html`, `BasiliskGame.js`, and `BasiliskGame.wasm`. Browsers must
load them over HTTP, not directly with `file://`. From the repository root:

```bash
python3 -m http.server 8000 --directory build-web/clients/game
```

Then open <http://localhost:8000/BasiliskGame.html>. On Windows, use `python`
or `py -3` instead of `python3` if needed.

### Developer visual debug harness

After building, start the local static-file/action bridge from the repository
root:

```bash
python3 clients/web-debug/server.py
```

On Windows, use `python clients/web-debug/server.py` (or `py
clients/web-debug/server.py`). Then open
<http://localhost:8765/clients/web-debug/index.html> and, in a second terminal,
start either the normal two-player visual CLI or solo debug mode:

```bash
./build/clients/cli/BasiliskVisualCli 20260812 424242 --browser-actions
./build/clients/cli/BasiliskVisualCli 20260812 424242 --browser-actions --solo
```

In PowerShell, replace the executable path with
`.\build\clients\cli\Release\BasiliskVisualCli.exe` when using CMake's default
Visual Studio multi-configuration generator, or locate it under the selected
configuration directory if a different configuration was built.
