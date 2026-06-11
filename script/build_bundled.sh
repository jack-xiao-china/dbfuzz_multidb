#!/bin/bash
# =============================================================
# dbfuzz — Bundled Dependencies Build Script
# =============================================================
# Builds dbfuzz with all dependencies downloaded and compiled
# from source. No system DB client libraries required.
#
# Supported: CentOS 7/8/9, RHEL 7/8/9, Fedora, Ubuntu, Debian
#
# Usage:
#   bash script/build_bundled.sh
#   bash script/build_bundled.sh --skip-deps    # skip installing system build tools
# =============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-bundled"
SKIP_DEPS=false

for arg in "$@"; do
    case $arg in
        --skip-deps) SKIP_DEPS=true ;;
        --help|-h)
            echo "Usage: bash script/build_bundled.sh [--skip-deps]"
            echo ""
            echo "Options:"
            echo "  --skip-deps   Skip installing system build tools (use if already installed)"
            exit 0
            ;;
    esac
done

# =============================================================
# Step 1: Install build tools (if needed)
# =============================================================
if [ "$SKIP_DEPS" = false ]; then
    echo "=== Installing build tools ==="
    if command -v dnf &>/dev/null; then
        echo "Detected: dnf (Fedora/RHEL 8+/CentOS 8+)"
        sudo dnf install -y \
            gcc-c++ cmake make git \
            autoconf automake \
            openssl-devel zlib-devel \
            flex bison \
            pkgconfig
    elif command -v yum &>/dev/null; then
        echo "Detected: yum (CentOS 7/RHEL 7)"
        sudo yum install -y \
            gcc-c++ cmake3 make git \
            autoconf automake \
            openssl-devel zlib-devel \
            flex bison \
            pkgconfig
        # CentOS 7 has cmake3 but the binary is 'cmake3'
        if ! command -v cmake &>/dev/null && command -v cmake3 &>/dev/null; then
            echo "Linking cmake3 → cmake"
            sudo ln -sf "$(which cmake3)" /usr/local/bin/cmake
        fi
    elif command -v apt-get &>/dev/null; then
        echo "Detected: apt-get (Debian/Ubuntu)"
        sudo apt-get update -qq
        sudo apt-get install -y \
            g++ cmake make git \
            autoconf automake \
            libssl-dev zlib1g-dev \
            flex bison \
            pkg-config
    else
        echo "WARNING: Unknown package manager. Please install manually:"
        echo "  gcc-c++ cmake make git autoconf automake openssl-devel zlib-devel flex bison"
    fi
    echo ""
fi

# =============================================================
# Step 2: Configure with bundled deps
# =============================================================
echo "=== Configuring with bundled dependencies ==="
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DUSE_BUNDLED_DEPS=ON \
    -DCMAKE_BUILD_TYPE=Release

# =============================================================
# Step 3: Build
# =============================================================
echo "=== Building dbfuzz (this may take 5-10 minutes on first run) ==="
cmake --build "$BUILD_DIR" -j"$(nproc)"

# =============================================================
# Step 4: Verify
# =============================================================
BINARY="$BUILD_DIR/dbfuzz"
if [ ! -f "$BINARY" ]; then
    BINARY="$BUILD_DIR/src/dbfuzz"
fi

if [ -f "$BINARY" ]; then
    echo ""
    echo "========================================="
    echo "  Build successful!"
    echo "========================================="
    echo "  Binary: $BINARY"
    echo "  Size:   $(du -h "$BINARY" | cut -f1)"
    echo ""
    echo "  Verify:"
    echo "    $BINARY --help"
    echo ""
    echo "  Config macros:"
    grep -E "^#define (HAVE_|PQXX_|PostgreSQL_)" "$BUILD_DIR/config.h" 2>/dev/null || true
    echo "========================================="
else
    echo "ERROR: Binary not found after build!"
    exit 1
fi
