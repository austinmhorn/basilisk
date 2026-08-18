#!/usr/bin/env bash

set -euo pipefail

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
        fail "CMake 3.25 or newer is required (Ubuntu supplied $version). Use a supported Ubuntu release whose apt repository provides CMake 3.25+, then rerun this script."
    fi
}

if [[ ! -r /etc/os-release ]]; then
    fail "Cannot identify this operating system. This installer supports Ubuntu only."
fi

# shellcheck disable=SC1091
source /etc/os-release
[[ "${ID:-}" == "ubuntu" ]] || fail "This installer supports Ubuntu only (detected ${PRETTY_NAME:-unknown})."
command -v apt-get >/dev/null 2>&1 || fail "apt-get is required but was not found."

apt_prefix=()
if ((EUID != 0)); then
    command -v sudo >/dev/null 2>&1 || fail "Root privileges are required. Install sudo or rerun this script as root."
    sudo -v || fail "Unable to obtain sudo privileges."
    apt_prefix=(sudo)
fi

log "Updating apt package metadata..."
"${apt_prefix[@]}" apt-get update

log "Installing native build tools and SDL3 desktop-video build prerequisites..."
"${apt_prefix[@]}" apt-get install -y \
    git \
    cmake \
    python3 \
    build-essential \
    pkg-config \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxfixes-dev \
    libxi-dev \
    libxss-dev \
    libxtst-dev \
    libxkbcommon-dev \
    libgl1-mesa-dev \
    libegl1-mesa-dev \
    libgles2-mesa-dev \
    libudev-dev

log "Verifying development tools..."
git --version
cmake --version | head -n 1
verify_cmake_version
python3 --version
if command -v g++ >/dev/null 2>&1; then
    g++ --version | head -n 1
else
    c++ --version | head -n 1
fi

log "Dependencies are ready."
