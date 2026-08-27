#!/usr/bin/env bash
# Android device orchestration for specs/004 US3 (push / run / clean-device).
#
# Invoked by mk/android.mk; all knobs come from the environment:
#   DEVICE          explicit serial (highest priority)
#   ANDROID_SERIAL  adb-native selection (medium priority)
#   DEVICE_DIR      device work dir (default /data/local/tmp/cpp_network)
#   ADB             adb binary (default "adb"); overridable by tests via a shim
#
# Subcommands:
#   serial         resolve + echo the selected device serial
#   push           build (prereq) and push artifacts + certs, byte summaries
#   run            remote exec of device_e2e against public HTTPS endpoints,
#                  streaming output, propagating exit code (external mode)
#   clean          remove $DEVICE_DIR contents (idempotent)
set -uo pipefail

ADB="${ADB:-adb}"

DEVICE_DIR="${DEVICE_DIR:-/data/local/tmp/cpp_network}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST_BIN_DIR="${BAZEL_BIN:-$REPO_ROOT/bazel-bin}"
SENTINEL="__NETLIB_EXIT__"



die() { echo "ERROR: $*" >&2; exit "${2:-1}"; }

# ---- device selection -----------------------------------------------------
list_devices() {
  "$ADB" devices 2>/dev/null | tail -n +2 | awk 'NF>=2 {print $1, $2}'
}

selected_serial() {
  local usable=() other=()
  while read -r sn st; do
    [ -z "$sn" ] && continue
    if [ "$st" = "device" ]; then usable+=("$sn");
    else other+=("$sn ($st)"); fi
  done < <(list_devices)

  if [ -n "${DEVICE:-}" ]; then
    for sn in "${usable[@]:-}"; do
      [ "$sn" = "$DEVICE" ] && { echo "$DEVICE"; return 0; }
    done
    die "requested DEVICE='$DEVICE' is not an available 'device' state. ${other[*]:-}"
  fi
  if [ -n "${ANDROID_SERIAL:-}" ]; then DEVICE="$ANDROID_SERIAL"
    for sn in "${usable[@]:-}"; do
      [ "$sn" = "$DEVICE" ] && { echo "$DEVICE"; return 0; }
    done
    die "requested ANDROID_SERIAL='$DEVICE' is not available. ${other[*]:-}"
  fi

  case ${#usable[@]} in
    0) if [ ${#other[@]} -gt 0 ]; then
         die "no authorized device; found unusable entries: ${other[*]}. Unlock screen & accept USB debugging."
       else
         die "no devices attached; check USB connection/enumeration."
       fi ;;
    1) echo "${usable[0]}" ;;
    *) die "multiple devices: ${usable[*]} — rerun with DEVICE=<serial>" ;;
  esac
}

SER=""
select_device_or_exit() {
  # selected_serial dies on invalid selection; without this explicit
  # propagation check the subshell's non-zero status would be swallowed and
  # orchestration would continue against an empty serial.
  SER=$(selected_serial) || exit $?
  echo "[$(date +%T)] device: $SER"
}

sflag() { echo "-s $SER"; }

# ---- push -----------------------------------------------------------------
stage_binaries() {
  # Prefer the android-config outputs when present (built by make
  # build-android); fall back to whatever the workspace symlink exposes.
  local dev="$REPO_ROOT/bazel-bin/src/tests/device_e2e"
  [ -f "$dev" ] || die "device binary missing — run 'make build-android' first" 3
  echo "$dev"
}

do_push() {
  select_device_or_exit >/dev/null
  local bin remote_files=("$DEVICE_DIR/device_e2e") total=0
  PUSHLOG="/tmp/cpp_network_push_summary.log"
  : > "$PUSHLOG"

  local bin
  bin="$(stage_binaries)"
  local remote_files=("$DEVICE_DIR/device_e2e")

  # cert assets (all *.pem under src/tests/certs)
  local certs=("$REPO_ROOT"/src/tests/certs/*.pem)
  "$ADB" $(sflag) shell "mkdir -p '$DEVICE_DIR/certs'" || die "mkdir on device failed" 4
  for f in "${certs[@]}"; do
    local name size
    name="$(basename "$f")"
    size="$(wc -c < "$f" | tr -d ' ')"
    printf "%8d B  certs/%s\n" "$size" "$name" | tee -a "$PUSHLOG"
    total=$((total+size))
  done
  local binsize
  binsize="$(wc -c < "$bin" | tr -d ' ')"
  printf "%8d B  device_e2e\n" "$binsize" | tee -a "$PUSHLOG"

  # uploads are streamed to stdout by adb; summaries already printed above.
  "$ADB" $(sflag) push "$bin" "$DEVICE_DIR/" || die "binary push failed (cable pulled mid-push? retry)" 5
  for f in "${certs[@]}"; do
    "$ADB" $(sflag) push "$f" "$DEVICE_DIR/certs/" || die "cert push failed ($f)" 5
  done

  # atomicity guard: every needed file must exist remotely now
  for r in "${remote_files[@]}"; do
    "$ADB" $(sflag) shell "test -f '$r'" || die "remote file missing after push: $r" 6
  done
  for f in "${certs[@]}"; do
    local name; name="$(basename "$f")"
    "$ADB" $(sflag) shell "test -f '$DEVICE_DIR/certs/$name'" || \
      die "remote cert missing after push: certs/$name" 6
  done
  echo "[android] push OK ($(basename "$bin") + ${#certs[@]} certs, $(($total+binsize)) bytes)"
}

ensure_pushed() {
  "$ADB" $(sflag) shell "test -f '$DEVICE_DIR/device_e2e'" >/dev/null 2>&1 || {
    echo "[android] remote artifacts missing — pushing..."
    do_push
  }
}
# ---- run ------------------------------------------------------------------
# External-internet validation mode (specs/004 revised scope): device and
# host share no network segment, so `run` executes the e2e against public
# HTTPS endpoints directly from the device's own connectivity — no host
# fixtures, no adb reverse, no PORTS. Local-fixture scenarios remain
# available via NETLIB_TEST_MODE=local on hosts running src/tests fixtures.
do_run() {
  select_device_or_exit
  ensure_pushed

  local envline="NETLIB_TEST_DATA_DIR=$DEVICE_DIR/certs"
  local cmd="cd '$DEVICE_DIR' && $envline ./device_e2e 2>&1; echo $SENTINEL:\$?"

  "$ADB" $(sflag) shell "$cmd" 2>&1 | tee /tmp/cpp_network_device_stream.log \
        | awk -v s="$SENTINEL:" '{ if (index($0, s)==1) { c=substr($0, length(s)+1); next } print; fflush() } END { fflush(); print c+0 > "/tmp/cpp_network_exit_code" }'
  local code
  code="$(cat /tmp/cpp_network_exit_code 2>/dev/null || echo 255)"
  rm -f /tmp/cpp_network_exit_code
  echo "[device-exit: $code]"
  exit "$code"
}

# ---- clean ----------------------------------------------------------------
do_clean() {
  select_device_or_exit >/dev/null
  "$ADB" $(sflag) shell "rm -rf '$DEVICE_DIR' && mkdir -p '$DEVICE_DIR'"
  echo "[android] cleaned $DEVICE_DIR"
}

case "${1:-}" in
  serial) selected_serial ;;
  push)   do_push ;;
  run)    do_run ;;
  clean)  do_clean ;;
  *) echo "usage: $0 {serial|push|run|clean}" >&2; exit 64 ;;
esac
