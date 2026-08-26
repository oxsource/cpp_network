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
