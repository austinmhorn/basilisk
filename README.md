# Basilisk

[![CI](https://github.com/austinmhorn/basilisk/actions/workflows/ci.yml/badge.svg?branch=dev)](https://github.com/austinmhorn/basilisk/actions/workflows/ci.yml)

Beware the Basilisk V2 — a deterministic C++ cave-hunting game engine designed to support native desktop, browser/WebAssembly, and authoritative multiplayer server clients from one shared core.

## Development

The active development branch is `dev`.

```bash
git switch dev
git pull origin dev
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
