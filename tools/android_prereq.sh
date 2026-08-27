#!/usr/bin/env bash
# Android toolchain prerequisite gate (specs/004 contracts/make-targets.md).
# Strict mode for make targets: exits 0 when ANDROID_NDK_HOME points at an
# NDK r26+ root; otherwise prints actionable diagnostics and exits 1.
# Informational detection lives in tools/platform_setup.sh.
set -u

root="${ANDROID_NDK_HOME:-}"
if [ -z "$root" ]; then
  echo "ERROR: ANDROID_NDK_HOME is not set." >&2
elif [ ! -f "$root/source.properties" ]; then
  echo "ERROR: ANDROID_NDK_HOME='$root' is not a valid NDK root." >&2
else
  rev="$(grep -E '^Pkg.Revision[[:space:]]*=' "$root/source.properties" | head -1 | sed 's/^[^=]*=[[:space:]]*//')"
  major="${rev%%.*}"
  case "$major" in ''|*[!0-9]*) major=0 ;; esac
  if [ "$major" -ge 26 ]; then
    exit 0
  fi
  echo "ERROR: NDK $rev is too old (requires r26+): $root" >&2
fi

echo "Hint: bash tools/platform_setup.sh detects installs and prints export lines," >&2
echo "      e.g. download r26+ from developer.android.com/ndk/downloads." >&2
exit 1
