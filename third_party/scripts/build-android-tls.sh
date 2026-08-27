#!/usr/bin/env bash
# Cross-build pinned OpenSSL 3.0.13 + libcurl 8.7.1 static libraries for
# Android arm64-v8a inside a Bazel genrule sandbox (specs/004 research.md D2).
#
# Usage: build-android-tls.sh <openssl-src> <curl-src> <install-dir>
#
# Environment: ANDROID_NDK_HOME must point at NDK r26+ (exported to the action
# via build:android_arm64 --action_env=ANDROID_NDK_HOME in .bazelrc).
#
# Outputs (matching declared genrule outs under //third_party/androidtls):
#   <install-dir>/lib/{libcrypto.a,libssl.a,libcurl.a}
#   <install-dir>/include/openssl/*.h        (chained curl build input only)
#   <install-dir>/include/curl/*.h           (public consumer headers)
set -euo pipefail

OPENSSL_SRC="$1"
CURL_SRC="$2"
INSTALL_DIR="$3"

: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at NDK r26+}"
[ -f "$ANDROID_NDK_HOME/source.properties" ] || {
  echo "ERROR: invalid NDK root: $ANDROID_NDK_HOME" >&2; exit 1;
}

case "$(uname -s)" in
  Darwin) HOST_TAG=darwin-x86_64 ;;   # NDK macOS toolchain is x86_64-hosted (runs natively on Apple Silicon too)
  Linux)
    case "$(uname -m)" in
      x86_64) HOST_TAG=linux-x86_64 ;;
      aarch64) HOST_TAG=linux-aarch64 ;;
      *) echo "ERROR: unsupported arch $(uname -m)" >&2; exit 1 ;;
    esac ;;
  *) echo "ERROR: unsupported host OS $(uname -s)" >&2; exit 1 ;;
esac

TRIPLE=aarch64-linux-android
API=24
NCPU=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

TOOLCHAIN_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG/bin"
[ -x "$TOOLCHAIN_BIN/$TRIPLE$API-clang" ] || {
  echo "ERROR: missing $TOOLCHAIN_BIN/$TRIPLE$API-clang" >&2; exit 1;
}
export PATH="$TOOLCHAIN_BIN:$PATH"
export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"

echo "[android-tls] building OpenSSL 3.0.13 -> $INSTALL_DIR"
mkdir -p "$PWD/work"
cp -R "$OPENSSL_SRC"/. "$PWD/work/openssl/"
cd "$PWD/work/openssl"
./Configure android-arm64 \
    "-D__ANDROID_API__=$API" \
    no-shared no-tests no-legacy no-comp \
    "-fPIC" \
    "--prefix=$INSTALL_DIR"
make -j "$NCPU" build_sw >/dev/null
make install_sw >/dev/null
test -f "$INSTALL_DIR/lib/libssl.a"
test -f "$INSTALL_DIR/lib/libcrypto.a"

cd "$PWD/../.."
echo "[android-tls] building libcurl 8.7.1"
cp -R "$CURL_SRC"/. "$PWD/work/curl/"
test -f "$PWD/work/curl/configure" || { echo "ERROR: curl configure missing after copy" >&2; exit 1; }
cd "$PWD/work/curl"
export CC="$TOOLCHAIN_BIN/$TRIPLE$API-clang"
export CXX="$TOOLCHAIN_BIN/$TRIPLE$API-clang++"
export AR="$TOOLCHAIN_BIN/llvm-ar"
export RANLIB="$TOOLCHAIN_BIN/llvm-ranlib"
export STRIP="$TOOLCHAIN_BIN/llvm-strip"
export CPPFLAGS="-I$INSTALL_DIR/include"
export LDFLAGS="-L$INSTALL_DIR/lib"

./configure \
    --host="$TRIPLE" \
    --with-openssl="$INSTALL_DIR" \
    --without-libpsl --without-libidn2 --without-brotli --without-zstd \
    --without-zlib --without-librtmp --without-nghttp2 --without-libssh2 \
    --without-ca-path --without-ca-bundle \
    --disable-shared --enable-static --enable-pic \
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

echo "[android-tls] OK: $INSTALL_DIR"

