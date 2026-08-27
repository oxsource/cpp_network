# Android cross-build convenience targets (specs/004 US2).
# Contracts: specs/004-android-https-push-run/contracts/make-targets.md
#
# Naming: every target owned by this module carries the `android_` prefix so
# future modules can never collide on generic verbs (push/run/clean).

# Register directly (single-owner module; duplicates caught by explicit check)
ifneq ($(filter android,$(MK_MODULES)),)
$(error duplicate module: android)
endif
MK_MODULES += android

# Resolve the NDK at parse time: an explicitly exported valid ANDROID_NDK_HOME
# wins; otherwise the newest r25+ install on the machine is picked. This makes
# `make android_*` work even when shell rc files pin an outdated NDK (the
# override is announced on stderr, never silent). Fails the whole invocation
# here — before any target runs — when nothing usable exists.
ANDROID_NDK_HOME := $(shell bash tools/android_prereq.sh)
ifeq ($(strip $(ANDROID_NDK_HOME)),)
$(error No usable Android NDK r25+ found — install one or export ANDROID_NDK_HOME; see tools/platform_setup.sh)
endif
export ANDROID_NDK_HOME

.PHONY: android_build
MK_TARGETS += android_build
android_build:
	@echo "[android] building cpp_network + device binaries (arm64-v8a, ndk=$$ANDROID_NDK_HOME)..."
	bazel build --config=android_arm64 \
		//src/public:cpp_network \
		//src/tests:device_e2e \
		//src/examples/http_demo:http_demo
	@echo "[android] android_build OK"

.PHONY: android_push
MK_TARGETS += android_push
android_push:
	bash tools/android_device.sh push

.PHONY: android_run
MK_TARGETS += android_run
android_run:
	bash tools/android_device.sh run

.PHONY: android_clean_device
MK_TARGETS += android_clean_device
android_clean_device:
	bash tools/android_device.sh clean

# One-click certificate verification on a connected device (specs/004 US1/US4):
#   build -> push -> RUN_MODE=local (S1-S7 self-signed/mTLS over adb reverse)
#                 -> RUN_MODE=external (E1-E3 public HTTPS with the system
#                    trust bundle merged from /system/etc/security/cacerts).
.PHONY: android_verify
MK_TARGETS += android_verify
android_verify: android_build android_push
	@echo "[android] certificate verification: local-fixture scenarios..."
	RUN_MODE=local bash tools/android_device.sh run
	@echo "[android] certificate verification: external internet scenarios..."
	bash tools/android_device.sh run
	@echo "[android] android_verify OK"

# Static assertion: the build graph must contain no system-libcurl link path
# (specs/005 FR-003). Fails with offending file list when violated.
.PHONY: deps_audit
MK_TARGETS += deps_audit
deps_audit:
	@echo "[audit] scanning for system-libcurl references..."
	@if grep -rn --include='BUILD*' --include='*.bzl' '\-lcurl' \
	     src third_party platforms BUILD.bazel WORKSPACE cpp_network_deps.bzl \
	     2>/dev/null; then \
	  echo "ERROR: system-libcurl references found (FR-003 violation)" >&2; exit 1; \
	fi
	@echo "[audit] OK: no -lcurl references in the Bazel graph"
