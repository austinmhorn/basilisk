#!/usr/bin/env bash

set -euo pipefail

readonly HOMEBREW_INSTALL_URL="https://brew.sh/"

install_web=false
if (($# > 1)); then
    printf 'Usage: %s [--with-web]\n' "$0" >&2
    exit 2
fi
if (($# == 1)); then
    [[ "$1" == "--with-web" ]] || {
        printf 'Usage: %s [--with-web]\n' "$0" >&2
        exit 2
    }
    install_web=true
fi

log() {
    printf '[Basilisk bootstrap] %s\n' "$*"
}

fail() {
    printf '[Basilisk bootstrap] ERROR: %s\n' "$*" >&2
    exit 1
}

verify_cmake_version() {
    local version major minor
    version="$(cmake --version | head -n 1 | awk '{print $3}')"
    IFS=. read -r major minor _ <<<"$version"
    if ((major < 3 || (major == 3 && minor < 25))); then
        fail "CMake 3.25 or newer is required (found $version). Run 'brew upgrade cmake' and rerun this script."
    fi
}

cmake_satisfies_minimum() {
    local version major minor
    command -v cmake >/dev/null 2>&1 || return 1
    version="$(cmake --version | head -n 1 | awk '{print $3}')"
    IFS=. read -r major minor _ <<<"$version"
    ((major > 3 || (major == 3 && minor >= 25)))
}

[[ "$(uname -s)" == "Darwin" ]] || fail "This installer supports macOS only."

if ! command -v xcode-select >/dev/null 2>&1; then
    fail "xcode-select is unavailable. Install Xcode Command Line Tools with: xcode-select --install"
fi

if ! xcode-select -p >/dev/null 2>&1; then
    log "Xcode Command Line Tools are required for Apple Clang."
    log "Starting Apple's installer. Complete it, then rerun this script."
    xcode-select --install || true
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    fail "Homebrew is required but was not found. Install it using the official instructions at ${HOMEBREW_INSTALL_URL}, then rerun this script."
fi

required_packages=(git cmake python)
if [[ "$install_web" == true ]]; then
    required_packages+=(emscripten)
fi

packages=()
for package in "${required_packages[@]}"; do
    if brew list --versions "$package" >/dev/null 2>&1; then
        log "$package is already installed."
    else
        packages+=("$package")
    fi
done

cmake_needs_upgrade=false
if brew list --versions cmake >/dev/null 2>&1 &&
    ! cmake_satisfies_minimum; then
    cmake_needs_upgrade=true
fi

if ((${#packages[@]})) || [[ "$cmake_needs_upgrade" == true ]]; then
    log "Updating Homebrew package metadata..."
    brew update
else
    log "Homebrew package metadata is current enough; skipping update."
fi

if ((${#packages[@]})); then
    log "Installing: ${packages[*]}"
    HOMEBREW_NO_AUTO_UPDATE=1 brew install "${packages[@]}"
else
    log "Homebrew dependencies are already installed."
fi

if [[ "$cmake_needs_upgrade" == true ]]; then
    log "Upgrading CMake to satisfy the project's current minimum version..."
    HOMEBREW_NO_AUTO_UPDATE=1 brew upgrade cmake
fi

log "Verifying development tools..."
git --version
cmake --version | head -n 1
verify_cmake_version
python3 --version
if command -v clang++ >/dev/null 2>&1; then
    clang++ --version | head -n 1
else
    c++ --version | head -n 1
fi

if [[ "$install_web" == true ]]; then
    emcc --version | head -n 1
    em++ --version | head -n 1
    emcmake --version
fi

if [[ "$install_web" == true ]]; then
    log "Native and optional WebAssembly dependencies are ready."
else
    log "Native dependencies are ready. Use --with-web to also install Emscripten."
fi
