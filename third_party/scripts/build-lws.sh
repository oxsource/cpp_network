#!/usr/bin/env bash
# Unified libwebsockets builder (specs/006 research.md D2).
#
# Usage: build-lws.sh <MODE> <openssl-install-dir> <install-dir>
#   MODE=android  cross-build for arm64-v8a via NDK (ANDROID_NDK_HOME r25+
#                 must be in the action environment; exports the same
#                 toolchain set as build-tls.sh's android branch)
#   MODE=host     native build with the machine's own CMake/cc
#
# Both modes compile the pinned libwebsockets from sources laid out under
# $PWD/work/lws/ (the calling genrule stages @lws//:sources there) and link
# against an ALREADY-BUILT OpenSSL install tree ($PWD work of the TLS
# bundle: include/openssl + lib/libssl.a + lib/libcrypto.a). There is no
# second TLS stack anywhere in this chain (specs/006 FR-003).
#
# Feature trim (specs/006 spec FR-012 + Assumptions):
#   client-only, no permessage-deflate extensions, static archive only,
#   no test apps/examples, no external event loops (built-in poll).
#
# NOTE for genrule wiring (Phase-2 tasks T004/T005): Bazel actions do not
# inherit the caller's PATH, so cmake is located defensively below rather
# than assumed on PATH.
set -euo pipefail

MODE="$1"
OPENSSL_DIR="$2"
INSTALL_DIR="$3"

case "$MODE" in
  android|host) ;;
  *) echo "ERROR: unknown lws build mode '$MODE'" >&2; exit 64 ;;
esac

if [ "$MODE" = "android" ]; then
  : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at NDK r25+}"
  [ -f "$ANDROID_NDK_HOME/source.properties" ] || {
    echo "ERROR: invalid NDK root: $ANDROID_NDK_HOME" >&2; exit 1;
  }
  TRIPLE=aarch64-linux-android
  API=24
  case "$(uname -s)" in
    Darwin) HOST_TAG=darwin-x86_64 ;;
    Linux)
      case "$(uname -m)" in
        x86_64) HOST_TAG=linux-x86_64 ;;
        aarch64) HOST_TAG=linux-aarch64 ;;
        *) echo "ERROR: unsupported host arch $(uname -m)" >&2; exit 1 ;;
      esac ;;
    *) echo "ERROR: unsupported host OS $(uname -s)" >&2; exit 1 ;;
  esac
  TOOLCHAIN_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG/bin"
  [ -x "$TOOLCHAIN_BIN/$TRIPLE$API-clang" ] || {
    echo "ERROR: missing $TOOLCHAIN_BIN/$TRIPLE$API-clang" >&2; exit 1;
  }
  export PATH="$TOOLCHAIN_BIN:$PATH"
  export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
  export CC="$TOOLCHAIN_BIN/$TRIPLE$API-clang"
  export CXX="$TOOLCHAIN_BIN/$TRIPLE$API-clang++"
  export AR="$TOOLCHAIN_BIN/llvm-ar"
  export RANLIB="$TOOLCHAIN_BIN/llvm-ranlib"
  export STRIP="$TOOLCHAIN_BIN/llvm-strip"
else
  # Defensive BSD ar on macOS (build-tls.sh finding: Homebrew GNU-ish tools).
  case "$(uname -s)" in
    Darwin) export AR=/usr/bin/ar; export RANLIB=/usr/bin/ranlib ;;
  esac
fi

CMAKE_BIN="$(command -v cmake || true)"
[ -n "$CMAKE_BIN" ] || for c in /opt/homebrew/bin/cmake \
                               /usr/local/bin/cmake \
                               /usr/local/opt/cmake/bin/cmake; do
  [ -x "$c" ] && { CMAKE_BIN="$c"; break; }
done
[ -n "$CMAKE_BIN" ] || {
  echo "ERROR: cmake not found (see tools/platform_setup.sh hint)" >&2; exit 1;
}

SRC="$PWD/work/lws"
BUILD_DIR="$PWD/work/lws-build"
[ -d "$SRC" ] || { echo "ERROR: staged lws sources missing at $SRC" >&2; exit 1; }
test -f "$OPENSSL_DIR/include/openssl/ssl.h" || {
  echo "ERROR: openssl install tree incomplete at $OPENSSL_DIR" >&2; exit 1;
}

mkdir -p "$BUILD_DIR"
CROSS_ARGS=""
if [ "$MODE" = "android" ]; then
  # Declare the target system explicitly: without this, host CMake injects
  # macOS-only '-arch arm64' into the NDK clang wrapper (which rejects it).
  CROSS_ARGS="-DCMAKE_SYSTEM_NAME=Android -DCMAKE_SYSTEM_VERSION=${API} \
    -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a -DCMAKE_OSX_SYSROOT= \
    -DCMAKE_OSX_ARCHITECTURES= \
    -DCMAKE_FIND_ROOT_PATH=${OPENSSL_DIR} \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH"
fi
"$CMAKE_BIN" -S "$SRC" -B "$BUILD_DIR" \
  $CROSS_ARGS \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -fvisibility=hidden -fPIC" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  -DLWS_WITH_STATIC=ON \
  -DLWS_WITH_SHARED=OFF \
  -DLWS_WITHOUT_SERVER=ON \
  -DLWS_WITHOUT_EXTENSIONS=ON \
  -DLWS_WITHOUT_TESTAPPS=ON \
  -DLWS_WITH_SSL=ON \
  -DOPENSSL_ROOT_DIR="$OPENSSL_DIR" \
  -DOPENSSL_USE_STATIC_LIBS=TRUE \
  -DLWS_WITH_ZLIB=OFF \
  -DLWS_WITH_HTTP_STREAM_COMPRESSION=OFF \
  -DLWS_WITH_LIBUV=OFF \
  -DLWS_WITH_LIBEV=OFF \
  -DLWS_WITH_LIBEVENT=OFF \
  -DLWS_WITH_GLIB=OFF \
  -DLWS_STATIC_PIC=ON \
  > "$PWD/work/lws-cmake.log" 2>&1

NPROC=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
"$CMAKE_BIN" --build "$BUILD_DIR" -j "$NPROC" >> "$PWD/work/lws-cmake.log" 2>&1
"$CMAKE_BIN" --install "$BUILD_DIR" >> "$PWD/work/lws-cmake.log" 2>&1

test -f "$INSTALL_DIR/lib/libwebsockets.a"
echo "[lws:$MODE] OK: $INSTALL_DIR"
