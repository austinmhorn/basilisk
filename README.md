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

```bash
git switch dev
git pull origin dev
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
