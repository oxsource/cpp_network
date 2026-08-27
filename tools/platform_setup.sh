#!/usr/bin/env bash
# Detects the host OS and architecture, then writes a git-ignored .user.bazelrc
# with the matching platform config. Mirrors graph_runtime's platform_setup.sh.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
USER_BAZELRC="$REPO_ROOT/.user.bazelrc"

os="$(uname -s)"
arch="$(uname -m)"

case "$os" in
  Darwin)
    case "$arch" in
      arm64)   platform="macos_arm64" ;;
      x86_64)  platform="macos_x86_64" ;;
      *) echo "ERROR: unsupported macOS architecture '$arch' (supported: arm64, x86_64)" >&2; exit 1 ;;
    esac
    ;;
  Linux)
    case "$arch" in
      x86_64)  platform="linux_x86_64" ;;
      aarch64) platform="linux_aarch64" ;;
      arm64)   platform="linux_aarch64" ;;
      *) echo "ERROR: unsupported Linux architecture '$arch' (supported: x86_64, aarch64)" >&2; exit 1 ;;
    esac
    ;;
  *)
    echo "ERROR: unsupported OS '$os' (supported: macOS, Linux)" >&2
    exit 1
    ;;
esac

printf 'build --config=%s\n' "$platform" > "$USER_BAZELRC"
echo "[platform_setup] detected $os/$arch -> $platform"
echo "[platform_setup] wrote $USER_BAZELRC"

# --- Android toolchain readiness checks (specs/004 T002) --------------------
# Informational only: exits 0 regardless so host-only workflows stay green.
# The android_build target performs the authoritative fail-fast check later
# (contracts/make-targets.md).

check_ndk() {
  local root="${ANDROID_NDK_HOME:-}"
  if [ -z "$root" ]; then
    echo "[platform_setup] NDK    : ANDROID_NDK_HOME not set"
    local candidate
    for candidate in "$HOME"/Library/Android/sdk/ndk/* "$HOME"/Android/Sdk/ndk/* \
                     /opt/android-ndk* /usr/local/android-ndk*; do
      if [ -f "$candidate/source.properties" ]; then
        echo "[platform_setup]         detected candidate:"
        echo "[platform_setup]           export ANDROID_NDK_HOME=$candidate"
      fi
    done
    echo "[platform_setup]         install hint: download NDK r25+ from"
    echo "[platform_setup]           developer.android.com/ndk/downloads"
    return 0
  fi
  if [ ! -f "$root/source.properties" ]; then
    echo "[platform_setup] NDK    : invalid ANDROID_NDK_HOME ('$root' lacks source.properties)"
    return 0
  fi
  local rev major
  # NDK source.properties spells it "Pkg.Revision = 26.1.x" (spaces around '=').
  rev="$(grep -E '^Pkg.Revision[[:space:]]*=' "$root/source.properties" | head -1 | sed 's/^[^=]*=[[:space:]]*//' || true)"
  major="${rev%%.*}"
  case "$major" in
    ''|*[!0-9]*) major=0 ;;
  esac
  if [ "$major" -ge 25 ]; then
    echo "[platform_setup] NDK    : ok ($rev at $root)"
  else
    echo "[platform_setup] NDK    : too old ($rev, requires r25+): $root"
    echo "[platform_setup]         hint: install a newer NDK and update ANDROID_NDK_HOME"
  fi
}

check_adb() {
  if command -v adb >/dev/null 2>&1; then
    echo "[platform_setup] adb    : ok ($(command -v adb))"
    return
  fi
  echo "[platform_setup] adb    : not found in PATH"
  echo "[platform_setup]         hint: export PATH=\"\$PATH:$HOME/Library/Android/sdk/platform-tools\""
}

check_ndk
check_adb

exit 0
