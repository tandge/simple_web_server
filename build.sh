#!/bin/bash
# LightWebServer 一键构建脚本
# 用法: ./build.sh [lin64|win64|all]
#   不带参数默认构建 all（lin64 + win64）

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

build_lin64() {
    info "Building Linux x86_64 ..."
    mkdir -p build/lin64
    cd build/lin64
    cmake ../.. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --parallel
    cd "$SCRIPT_DIR"

    if [ -f "build/lin64/webserver" ]; then
        info "Linux build OK: build/lin64/webserver ($(stat -c%s build/lin64/webserver) bytes)"
    else
        error "Linux build FAILED"
        return 1
    fi
}

build_win64() {
    info "Building Windows x86_64 (cross-compile) ..."
    if ! command -v x86_64-w64-mingw32-g++ &>/dev/null; then
        warn "x86_64-w64-mingw32-g++ not found, skipping Windows build"
        warn "Install: sudo apt install mingw-w64"
        return 0
    fi

    mkdir -p build/win64
    cd build/win64
    cmake ../.. \
        -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/toolchain-mingw64.cmake" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build . --parallel
    cd "$SCRIPT_DIR"

    if [ -f "build/win64/webserver.exe" ]; then
        info "Windows build OK: build/win64/webserver.exe ($(stat -c%s build/win64/webserver.exe) bytes)"
    else
        error "Windows build FAILED"
        return 1
    fi
}

# 解析参数
TARGET="${1:-all}"

case "$TARGET" in
    lin64)
        build_lin64
        ;;
    win64)
        build_win64
        ;;
    all)
        build_lin64
        build_win64
        ;;
    *)
        echo "Usage: $0 [lin64|win64|all]"
        exit 1
        ;;
esac

echo ""
info "Build complete."
echo ""
echo "Products:"
[ -f "build/lin64/webserver" ]     && echo "  build/lin64/webserver       (Linux x86_64)"
[ -f "build/win64/webserver.exe" ] && echo "  build/win64/webserver.exe   (Windows x86_64)"
