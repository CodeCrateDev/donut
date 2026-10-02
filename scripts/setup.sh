#!/usr/bin/env bash
#
# donut - bootstrap installer
#
# Install donut on a clean machine (note: pipe into bash, not sh):
#
#     curl -fsSL https://raw.githubusercontent.com/<you>/donut/main/scripts/setup.sh | bash
#
# Or run from inside a checkout of the repository (the clone step is skipped):
#
#     ./scripts/setup.sh
#
# What it does:
#   1. clone the repository (unless already run from a checkout)
#   2. check build dependencies (git, cmake >= 3.16, make, C++20 compiler)
#   3. build every tool (Release)
#   4. copy the binaries to ~/.donut/bin
#   5. symlink each binary into /usr/bin (needs root or sudo)
#
# Environment overrides:
#   DONUT_REPO    repository to clone when not run from a checkout
#   INSTALL_DIR   binary destination (default: ~/.donut/bin)
#   LINK_DIR      symlink directory (default: /usr/bin)
#   BUILD_TYPE    CMake build type (default: Release)

set -euo pipefail

DONUT_REPO="${DONUT_REPO:-}"
INSTALL_DIR="${INSTALL_DIR:-$HOME/.donut/bin}"
LINK_DIR="${LINK_DIR:-/usr/bin}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

say() { printf '  ==> %s\n' "$*"; }
err() { printf 'error: %s\n' "$*" >&2; }

# --- 1. Locate the source tree -------------------------------------------
# Either the checkout this script lives in, or a fresh clone in a temp dir.

tmpdir=""
cleanup() {
    [ -n "$tmpdir" ] && rm -rf "$tmpdir" || true
}
trap cleanup EXIT

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$script_dir/../CMakeLists.txt" ]; then
    src_dir="$(cd "$script_dir/.." && pwd)"
    say "Building from existing checkout at $src_dir"
else
    if [ -z "$DONUT_REPO" ]; then
        err "no repository to build."
        err "Run this script from inside a donut checkout, or set the"
        err "repository URL, e.g.:"
        err "    DONUT_REPO=https://github.com/<you>/donut.git bash setup.sh"
        exit 1
    fi
    say "Cloning $DONUT_REPO"
    tmpdir="$(mktemp -d)"
    git clone --depth 1 "$DONUT_REPO" "$tmpdir/donut"
    src_dir="$tmpdir/donut"
fi
src_dir="$(cd "$src_dir" && pwd)"

# --- 2. Check dependencies ------------------------------------------------

say "Checking dependencies"

missing=()
need() {
    command -v "$1" >/dev/null 2>&1 || missing+=("$1")
}

[ -n "$tmpdir" ] && need git
need cmake
need make

if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    missing+=("g++ (or clang++)")
fi

if [ "${#missing[@]}" -gt 0 ]; then
    err "missing required programs: ${missing[*]}"
    err "on Ubuntu/Debian install them with:"
    err "    sudo apt-get install -y git build-essential cmake"
    exit 1
fi

cmver="$(cmake --version | head -n1 | awk '{print $NF}')"
if [ "$(printf '%s\n' 3.16 "$cmver" | sort -V | head -n1)" != "3.16" ]; then
    err "cmake >= 3.16 required (found $cmver)"
    exit 1
fi

cxx_ok() {
    command -v "$1" >/dev/null 2>&1 || return 1
    [ "$("$1" -dumpversion | cut -d. -f1)" -ge 10 ]
}
if ! cxx_ok g++ && ! cxx_ok clang++; then
    err "a C++20 compiler is required (g++ >= 10 or clang++ >= 10)"
    exit 1
fi

# --- 3. Build --------------------------------------------------------------

say "Configuring ($BUILD_TYPE)"
cmake -S "$src_dir" -B "$src_dir/build" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

say "Building"
cmake --build "$src_dir/build" -j "$(nproc 2>/dev/null || echo 1)"

bin_src="$src_dir/build/bin"
if [ ! -d "$bin_src" ] || [ -z "$(ls -A "$bin_src" 2>/dev/null)" ]; then
    err "build produced no binaries in $bin_src"
    exit 1
fi

# --- 4. Copy binaries to ~/.donut/bin --------------------------------------

say "Installing binaries to $INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
cp -f "$bin_src"/* "$INSTALL_DIR"/

# --- 5. Symlink into /usr/bin ----------------------------------------------

say "Symlinking into $LINK_DIR"
if [ "$(id -u)" -eq 0 ]; then
    as_root=""
elif command -v sudo >/dev/null 2>&1; then
    as_root="sudo"
else
    err "root (or sudo) is required to symlink into $LINK_DIR"
    exit 1
fi

for exe in "$INSTALL_DIR"/donut-*; do
    [ -e "$exe" ] || continue
    $as_root ln -sfn "$exe" "$LINK_DIR/$(basename "$exe")"
done

say "Installed commands:"
for exe in "$INSTALL_DIR"/donut-*; do
    [ -e "$exe" ] || continue
    printf '    %s\n' "$(basename "$exe")"
done
