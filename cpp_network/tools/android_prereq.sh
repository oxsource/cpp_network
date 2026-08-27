#!/usr/bin/env bash
# Android NDK resolution for specs/004 make targets.
#
#   tools/android_prereq.sh            -> print ONE resolved NDK root path
#
# Selection order:
#   1. $ANDROID_NDK_HOME when it is a valid NDK r25+ root (explicit wins)
#   2. Otherwise the NEWEST valid r25+ install discovered under the usual SDK
#      locations — this rescues shells whose rc files pin an outdated NDK,
#      while keeping the decision visible via a stderr notice.
#
# stdout carries exactly one path; everything human-facing goes to stderr.
set -u

valid_rev() { # root -> prints revision if >= 26, else nothing
  local props="$1/source.properties"
  [ -f "$props" ] || return 0
  local rev major
  rev="$(grep -E '^Pkg.Revision[[:space:]]*=' "$props" | head -1 | sed 's/^[^=]*=[[:space:]]*//')"
  major="${rev%%.*}"
  case "$major" in ''|*[!0-9]*) major=0 ;; esac
  [ "$major" -ge 25 ] && printf '%s' "$rev"
}

hint() {
  echo "Hint: install NDK r25+ (developer.android.com/ndk/downloads)" >&2
  echo "Hint: or point ANDROID_NDK_HOME at it before running make." >&2
}

candidates=("$HOME"/Library/Android/sdk/ndk/* "$HOME"/Android/Sdk/ndk/* \
            /opt/android-ndk* /usr/local/android-ndk*)

# 1) explicit env override, validated.
if [ -n "${ANDROID_NDK_HOME:-}" ]; then
  rev="$(valid_rev "$ANDROID_NDK_HOME")"
  if [ -n "$rev" ]; then
    printf '%s' "$ANDROID_NDK_HOME"
    exit 0
  fi
  echo "NOTICE: ANDROID_NDK_HOME='$ANDROID_NDK_HOME' ignored (missing or older than r25)." >&2
fi

# 2) discovery: choose the highest revision >= 26.
best_root="" best_rev=""
for c in "${candidates[@]}"; do
  rev="$(valid_rev "$c")" || true
  [ -z "$rev" ] && continue
  if [ -z "$best_rev" ] || \
     [ "$(printf '%s\n%s' "$rev" "$best_rev" | sort -Vr | head -1)" = "$rev" ]; then
    best_root="$c"; best_rev="$rev"
  fi
done

if [ -n "$best_root" ]; then
  if [ -n "${ANDROID_NDK_HOME:-}" ]; then
    echo "NOTICE: using discovered NDK $best_rev ($best_root) instead of stale ANDROID_NDK_HOME." >&2
  fi
  printf '%s' "$best_root"
  exit 0
fi

echo "ERROR: no usable Android NDK r25+ found." >&2
hint
exit 1
