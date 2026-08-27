#!/usr/bin/env bash
# Unified TLS builder (specs/005 T001; rebuilt 2026-08-27 for L1 re-audit).
# build-android-tls.sh without changing its android behavior).
#
# Usage: build-tls.sh <MODE> <openssl-src> <curl-src> <install-dir>
#   MODE=android  cross-build for arm64-v8a via NDK (requires
#                 ANDROID_NDK_HOME r25+ in the action environment)
#   MODE=host     native build for the machine executing the action
#                 (macOS arm64/x86_64, Linux x86_64/aarch64)
#
# Shared invariants across modes:
#   * pinned sources only (OpenSSL 3.0.13 / curl 8.7.1)
#   * identical trim set for curl (HTTP/HTTPS, no extras)
#   * object-level symbol hiding: -fvisibility=hidden -fPIC everywhere
#     (specs/005 contracts/symbol-visibility.md L1)
set -euo pipefail

MODE="$1"
OPENSSL_SRC="$2"
CURL_SRC="$3"
INSTALL_DIR="$4"

case "$MODE" in
  android|host) ;;
  *) echo "ERROR: unknown TLS build mode '$MODE'" >&2; exit 64 ;;
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
  CURL_HOST="--host=$TRIPLE"
  OPENSSL_TARGET="android-arm64"
else
  # Native (or -arch cross on the same OS) with the sandbox toolchain;
  # defensive BSD-ar choice on macOS avoids the GNU-archive/ld64 trap observed
  # when Homebrew ar sneaks onto PATH. TLS_HOST_ARCH allows a macos_x86_64
  # config to request real x86_64 objects while running on an arm64 machine.
  CURL_HOST=""
  host_os="$(uname -s)"
  arch="${TLS_HOST_ARCH:-$(uname -m)}"
  case "$host_os-$arch" in
    Darwin-arm64)
      OPENSSL_TARGET=darwin64-arm64-cc ;;
    Darwin-x86_64)
      OPENSSL_TARGET=darwin64-x86_64-cc
      ARCH_FLAG="-arch x86_64"
      export AR=/usr/bin/ar
      export RANLIB=/usr/bin/ranlib ;;
    Linux-x86_64)  OPENSSL_TARGET=linux-x86_64 ;;
    Linux-aarch64) OPENSSL_TARGET=linux-aarch64 ;;
    *) echo "ERROR: unsupported host os/arch combination $host_os/$arch" >&2; exit 1 ;;
  esac
fi

NCPU=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
VIS_FLAGS="-fPIC"
if [ "${TLS_DISABLE_VIS:-}" != "1" ]; then VIS_FLAGS="-fvisibility=hidden $VIS_FLAGS"; fi
[ -n "${OPENSSL_OPTS_EXTRA:-}" ] && OPENSSL_TARGET="$OPENSSL_TARGET $OPENSSL_OPTS_EXTRA"

echo "[tls:$MODE] building OpenSSL 3.0.13 -> $INSTALL_DIR"
mkdir -p "$PWD/work"
cp -R "$OPENSSL_SRC"/. "$PWD/work/openssl/"
cd "$PWD/work/openssl"
./Configure "$OPENSSL_TARGET" \
    no-shared no-tests no-legacy no-comp \
    $VIS_FLAGS \
    "--prefix=$INSTALL_DIR"
make -j "$NCPU" build_sw >/dev/null
make install_sw >/dev/null
test -f "$INSTALL_DIR/lib/libssl.a"
test -f "$INSTALL_DIR/lib/libcrypto.a"

cd "$PWD/../.."
echo "[tls:$MODE] building libcurl 8.7.1"
cp -R "$CURL_SRC"/. "$PWD/work/curl/"
test -f "$PWD/work/curl/configure" || {
  echo "ERROR: curl configure missing after copy" >&2; exit 1;
}
cd "$PWD/work/curl"

export CFLAGS="${VIS_FLAGS} ${ARCH_FLAG:-}"
export CPPFLAGS="-I$INSTALL_DIR/include"
export LDFLAGS="-L$INSTALL_DIR/lib ${ARCH_FLAG:-}"
./configure \
    $CURL_HOST \
    --with-openssl="$INSTALL_DIR" \
    --without-libpsl --without-libidn2 --without-brotli --without-zstd \
    --without-zlib --without-librtmp --without-nghttp2 --without-libssh2 \
    --without-ca-path --without-ca-bundle \
    --disable-shared --enable-static \
    --disable-dependency-tracking \
    --enable-http \
    --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp \
    --disable-dict --disable-telnet --disable-tftp --disable-gopher \
    --disable-imap --disable-pop3 --disable-smtp --disable-scp --disable-sftp \
    --disable-mqtt --disable-websockets \
    "--prefix=$INSTALL_DIR"

make -j "$NCPU" >/dev/null
make install >/dev/null
test -f "$INSTALL_DIR/lib/libcurl.a"

echo "[tls:$MODE] OK: $INSTALL_DIR"
