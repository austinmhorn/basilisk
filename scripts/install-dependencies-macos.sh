#!/usr/bin/env bash

set -euo pipefail

readonly HOMEBREW_INSTALL_URL="https://brew.sh/"

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

log "Updating Homebrew package metadata..."
brew update

packages=()
for package in git cmake python; do
    if brew list --versions "$package" >/dev/null 2>&1; then
        log "$package is already installed."
    else
        packages+=("$package")
    fi
done

if ((${#packages[@]})); then
    log "Installing: ${packages[*]}"
    brew install "${packages[@]}"
else
    log "Homebrew dependencies are already installed."
fi

if brew outdated --quiet cmake | grep -qx 'cmake'; then
    log "Upgrading CMake to satisfy the project's current minimum version..."
    brew upgrade cmake
fi

# Future SFML extension point:
# Add the Homebrew "sfml" formula here when Basilisk introduces an SFML client.

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

log "Dependencies are ready."
