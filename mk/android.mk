# Android cross-build convenience targets (specs/004 US2).
# Contracts: specs/004-android-https-push-run/contracts/make-targets.md

# Register directly (single-owner module; duplicates caught by explicit check)
ifneq ($(filter android,$(MK_MODULES)),)
$(error duplicate module: android)
endif
MK_MODULES += android

ANDROID_PREREQ = tools/android_prereq.sh

.PHONY: build-android
MK_TARGETS += build-android
build-android:
	@echo "[android] checking toolchain prerequisites..."
	bash $(ANDROID_PREREQ)
	@echo "[android] building cpp_network + device binaries (arm64-v8a)..."
	bazel build --config=android_arm64 \
		//src/public:cpp_network \
		//src/tests:device_e2e \
		//src/examples/http_demo:http_demo
	@echo "[android] build-android OK"

.PHONY: push
MK_TARGETS += push
push:
	bash tools/android_device.sh push

.PHONY: run
MK_TARGETS += run
run:
	bash tools/android_device.sh run

.PHONY: clean-device
MK_TARGETS += clean-device
clean-device:
	bash tools/android_device.sh clean

# One-click certificate verification on a connected device (specs/004 US1/US4):
#   build -> push -> RUN_MODE=local (S1-S7 self-signed/mTLS over adb reverse)
#                 -> RUN_MODE=external (E1-E3 public HTTPS with the system
#                    trust bundle merged from /system/etc/security/cacerts).
.PHONY: verify-android
MK_TARGETS += verify-android
verify-android: build-android push
	@echo "[android] certificate verification: local-fixture scenarios..."
	RUN_MODE=local bash tools/android_device.sh run
	@echo "[android] certificate verification: external internet scenarios..."
	bash tools/android_device.sh run
	@echo "[android] verify-android OK"
