# Basilisk

[![CI](https://github.com/austinmhorn/basilisk/actions/workflows/ci.yml/badge.svg?branch=dev)](https://github.com/austinmhorn/basilisk/actions/workflows/ci.yml)

**Basilisk** is a pre-alpha deterministic C++ cave-hunting game engine designed to support native desktop, browser/WebAssembly, and authoritative multiplayer server clients from one shared core.

## Versioning

Game development and simulator-bot development use separate version lines:

- **Game:** pre-alpha `v0.x`
- **Simulation benchmark:** **BOT V3.14** is the current frozen balance/regression baseline.

`BOT V*` labels refer only to simulator strategy/telemetry iterations; they are not game versions.

See [`docs/CORE_CLIENT_BOUNDARY_AUDIT.md`](docs/CORE_CLIENT_BOUNDARY_AUDIT.md) for the current transition plan from simulation-first development to a playable client.

## Development

The active development branch is `dev`.

### Install dependencies

Basilisk currently requires Git, CMake 3.25 or newer, a C++20 toolchain, and
Python 3 for the visual debug bridge. It has no third-party C++ library
dependencies yet. Run the bootstrap script for your operating system from the
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

**Windows** (PowerShell, `winget`, and Visual Studio 2022 Build Tools/MSVC):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-dependencies-windows.ps1
```

This one-command process bypasses execution policy only for that invocation; it
does not change the global policy. The installer adds the Visual Studio C++
workload and recommended Windows SDK. Open a new PowerShell terminal afterward
if newly installed commands are not visible. No Visual Studio-specific CMake
generator is required; CMake selects an installed default generator.

SFML is intentionally not installed. Each bootstrap script contains a marked
extension point for adding it when Basilisk gains an SFML client.

### Build and test

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

### Visual debug client

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
