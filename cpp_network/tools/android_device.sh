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

# T019 hardening (specs/004 US4): every device-side write must stay inside
# /data/local/tmp/** — the only directory tree an adb shell user can write
# without root. Guards against empty or traversing overrides of DEVICE_DIR
# before any destructive op (rm -rf) or upload fires.
validate_device_dir() {
  case "$DEVICE_DIR" in
    /data/local/tmp/*|/data/local/tmp)
      case "$DEVICE_DIR" in
        *..*) die "unsafe DEVICE_DIR='$DEVICE_DIR' (must not contain ..)" 64 ;;
        *) return 0 ;;
      esac ;;
    *) die "unsafe DEVICE_DIR='$DEVICE_DIR' (must live under /data/local/tmp/)" 64 ;;
  esac
}

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
  # android_build); fall back to whatever the workspace symlink exposes.
  local dev="$REPO_ROOT/bazel-bin/src/tests/device_e2e"
  [ -f "$dev" ] || die "device binary missing — run 'make android_build' first" 3
  echo "$dev"
}

do_push() {
  validate_device_dir
  select_device_or_exit >/dev/null
  local bin remote_files=("$DEVICE_DIR/device_e2e") total=0
  PUSHLOG="/tmp/cpp_network_push_summary.log"
  : > "$PUSHLOG"

  local bin
  bin="$(stage_binaries)"
  local remote_files=("$DEVICE_DIR/device_e2e")

  # cert assets (all *.pem under src/tests/certs)
  local certs=("$REPO_ROOT"/src/tests/certs/*.pem)
  "$ADB" $(sflag) shell "mkdir -p '$DEVICE_DIR/certs' '$DEVICE_DIR/tmp'" || die "mkdir on device failed" 4
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

# Merge the Android system trust store into a single PEM bundle inside the
# confined work dir. This is the documented FR-003 pattern: Android offers no
# c_hash'ed CAPATH or single-file bundle consumable by libcurl/OpenSSL, so
# applications must derive their trust anchor by explicit injection.
stage_system_ca_bundle() {
  "$ADB" $(sflag) shell \
    "mkdir -p '$DEVICE_DIR/certs' && cat /system/etc/security/cacerts/* > '$DEVICE_DIR/certs/system_cacerts.pem'" \
    || die "failed to build system CA bundle on device" 6
  "$ADB" $(sflag) shell "test -s '$DEVICE_DIR/certs/system_cacerts.pem'" \
    || die "system CA bundle came out empty" 6
}
# ---- run ------------------------------------------------------------------
# Two validation modes (specs/004 contracts/device-test-contract.md):
#
# external (default): device_e2e hits public HTTPS endpoints using the
#   device's own connectivity — no host involvement, network-segment free.
# local (RUN_MODE=local): self-signed / mTLS scenarios S1-S7 need reachable
#   fixtures. `adb reverse` tunnels the DEVICE's 127.0.0.1:<port> back to the
#   HOST over the USB link, so host/device sharing no Wi-Fi segment is fine —
#   a cabled connection is what matters. Host fixtures are launched here and
#   torn down when done.
RUN_MODE="${RUN_MODE:-external}"
FIXTURE_PORTS="18080 18443 18444 18086 18446 18447 18088"
PIDS_FILE="/tmp/cpp_network_e2e_servers.pids"
LOGDIR="/tmp/cpp_network_e2e_logs"

port_listening() { nc -z 127.0.0.1 "$1" >/dev/null 2>&1; }

fixtures_up() {
  mkdir -p "$LOGDIR"
  python3 "$REPO_ROOT/src/tests/test_server.py" --port 18080 \
    >"$LOGDIR/http.log" 2>&1 &
  echo $! >> "$PIDS_FILE"
  python3 "$REPO_ROOT/src/tests/test_tls_server.py" --port 18443 \
    >"$LOGDIR/tls.log" 2>&1 &
  echo $! >> "$PIDS_FILE"
  python3 "$REPO_ROOT/src/tests/test_tls_server.py" --port 18444 \
    --require-client-cert >"$LOGDIR/mtls.log" 2>&1 &
  echo $! >> "$PIDS_FILE"
  python3 "$REPO_ROOT/src/tests/test_ws_server.py" --port 18086 \
    >"$LOGDIR/ws-plain.log" 2>&1 &
  echo $! >> "$PIDS_FILE"
  python3 "$REPO_ROOT/src/tests/test_ws_server.py" --port 18446 --tls \
    --certs-dir "$REPO_ROOT/src/tests/certs" >"$LOGDIR/ws-tls.log" 2>&1 &
  echo $! >> "$PIDS_FILE"
  python3 "$REPO_ROOT/src/tests/test_ws_server.py" --port 18447 --tls \
    --require-client-cert \
    --certs-dir "$REPO_ROOT/src/tests/certs" >"$LOGDIR/ws-mtls.log" 2>&1 &
  echo $! >> "$PIDS_FILE"
  python3 "$REPO_ROOT/src/tests/test_ws_server.py" --port 18088 \
    --peer-close 1000 --reason bye >"$LOGDIR/ws-peerclose.log" 2>&1 &
  echo $! >> "$PIDS_FILE"

  for i in $(seq 1 50); do
    if port_listening 18080 && port_listening 18443 && port_listening 18444 \
       && port_listening 18086 && port_listening 18446 && port_listening 18447 \
       && port_listening 18088; then
      echo "[android] host fixtures ready (:18080/:18443/:18444 + ws :18086/:18446/:18447/:18088)"
      return 0
    fi
    sleep 0.1
  done
  fixtures_down
  die "host fixtures failed to start within 5s — see $LOGDIR" 7
}

fixtures_down() {
  if [ -f "$PIDS_FILE" ]; then
    while read -r p; do kill "$p" 2>/dev/null || true; done < "$PIDS_FILE"
    rm -f "$PIDS_FILE"
  fi
}

reverse_up() {
  local p
  for p in $FIXTURE_PORTS; do
    "$ADB" $(sflag) reverse "tcp:$p" "tcp:$p" || die "adb reverse failed for tcp:$p" 7
  done
}
reverse_down() {
  local p
  for p in $FIXTURE_PORTS; do "$ADB" $(sflag) reverse --remove "tcp:$p" 2>/dev/null || true; done
}

do_run() {
  validate_device_dir
  select_device_or_exit
  ensure_pushed

  if [ "${RUN_MODE:-external}" != "local" ]; then
    # Verified-peer requests need an anchor; merge the device's own system
    # trust store into an injectable bundle (FR-003 documented pattern).
    stage_system_ca_bundle
  fi

  # TMPDIR: keep any scratch writes inside the confined work dir,
  # never /tmp (not writable for shell users).
  local envline="TMPDIR=$DEVICE_DIR/tmp NETLIB_TEST_DATA_DIR=$DEVICE_DIR NETLIB_TEST_EXT_CA_BUNDLE=$DEVICE_DIR/certs/system_cacerts.pem"

  if [ "$RUN_MODE" = "local" ]; then
    if [ ! -f "$REPO_ROOT/src/tests/test_tls_server.py" ]; then
      die "run from the repository root so fixture scripts resolve" 7
    fi
    trap fixtures_down EXIT INT TERM
    fixtures_up
    reverse_up
    envline="$envline NETLIB_TEST_MODE=local NETLIB_TEST_HTTP_BASE=http://127.0.0.1:18080 NETLIB_TEST_HTTPS_BASE=https://127.0.0.1:18443 NETLIB_TEST_MTLS_BASE=https://127.0.0.1:18444 NETLIB_TEST_WS_PLAIN_BASE=ws://127.0.0.1:18086/echo NETLIB_TEST_WS_TLS_BASE=wss://127.0.0.1:18446/secure NETLIB_TEST_WS_MTLS_BASE=wss://127.0.0.1:18447/secure NETLIB_TEST_WS_PEER_CLOSE_BASE=ws://127.0.0.1:18088/bye SSL_CERT_FILE=$DEVICE_DIR/certs/system_cacerts.pem"
  fi

  local cmd="cd '$DEVICE_DIR' && $envline ./device_e2e 2>&1; echo $SENTINEL:\$?"

  "$ADB" $(sflag) shell "$cmd" 2>&1 | tee /tmp/cpp_network_device_stream.log \
        | awk -v s="$SENTINEL:" '{ if (index($0, s)==1) { c=substr($0, length(s)+1); next } print; fflush() } END { fflush(); print c+0 > "/tmp/cpp_network_exit_code" }'

  if [ "${RUN_MODE:-}" = "local" ]; then reverse_down; fixtures_down; trap - EXIT INT TERM; fi
  local code
  code="$(cat /tmp/cpp_network_exit_code 2>/dev/null || echo 255)"
  rm -f /tmp/cpp_network_exit_code
  echo "[device-exit: $code]"
  exit "$code"
}

# ---- clean ----------------------------------------------------------------
do_clean() {
  validate_device_dir
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
