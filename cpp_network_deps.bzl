"""Idempotent external dependency bootstrap for the cpp_network workspace.

Mirrors graph_runtime's `graph_runtime_deps.bzl` pattern: every dependency is
guarded by `native.existing_rule(...)` so that repeated calls to `cpp_network_setup()`
are no-ops.

All sha256 values are real checksums of the pinned archives (verified).
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# rules_foreign_cc hosts configure/make style builds (OpenSSL, libcurl for the
# Android branch). Pinned 0.10.1: validated against Bazel 6.5; upgrade requires
# re-validating the third_party cross-compilation chain.
_FOREIGN_CC = {
    "sha256": "476303bd0f1b04cc311fc258f1708a5f6ef82d3091e53fd1977fa20383425a6a",
    "strip_prefix": "rules_foreign_cc-0.10.1",
    "urls": ["https://github.com/bazel-contrib/rules_foreign_cc/archive/refs/tags/0.10.1.tar.gz"],
}

# Versions are pinned and sha256-verified for reproducibility (SC-006).
_DEPS = {
    "bazel_skylib": {
        "sha256": "9f38886a40548c6e96c106b752f242130ee11aaa068a56ba7e56f4511f33e4f2",
        "urls": ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.6.1/bazel-skylib-1.6.1.tar.gz"],
    },
    "googletest": {
        "sha256": "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
        "urls": ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
        "strip_prefix": "googletest-1.14.0",
    },
    "curl": {
        # libcurl >= 7.86 for WebSocket support (v2). Pinned 8.7.1.
        # Official release tarball (ships a pre-generated ./configure —
        # GitHub source archives require autoreconf, which we do not run in
        # the sandbox). Consumed by third_party/androidtls on the android
        # config only.
        "sha256": "f91249c87f68ea00cf27c44fdfa5a78423e41e71b7d408e5901a9896d905c495",
        "urls": ["https://curl.se/download/curl-8.7.1.tar.gz"],
        "strip_prefix": "curl-8.7.1",
        "build_file": "//third_party/libcurl:curl_external.BUILD",
    },
    "openssl": {
        # OpenSSL 3.x LTS. Pinned 3.0.13. Used as the TLS backend on ALL
        # platforms (host macOS/Linux and Android).
        "sha256": "e74504ed7035295ec7062b1da16c15b57ff2a03cd2064a28d8c39458cacc45fc",
        "urls": ["https://github.com/openssl/openssl/archive/refs/tags/openssl-3.0.13.tar.gz"],
        "strip_prefix": "openssl-openssl-3.0.13",
        "build_file": "//third_party/openssl:openssl_external.BUILD",
    },
}

def _guard(rule, name, kwargs):
    if native.existing_rule(name):
        return
    rule(name = name, **kwargs)

# ---------------------------------------------------------------------------
# Android NDK (@androidndk) -- research.md D3.
#
# Adopted from the video_codec workspace's verified setup: rules_android_ndk
# (bazelbuild, v0.1.2) provides android_ndk_repository that supports modern
# NDK layouts and is fetched LAZILY, so hosts without ANDROID_NDK_HOME never
# touch it and non-Android builds stay green (FR-010). Toolchain registration
# happens only under --config=android_arm64 via .bazelrc --extra_toolchains.
_RULES_ANDROID_NDK = {
    "sha256": "65aedff0cd728bee394f6fb8e65ba39c4c5efb11b29b766356922d4a74c623f5",
    "strip_prefix": "rules_android_ndk-0.1.2",
    "urls": ["https://github.com/bazelbuild/rules_android_ndk/releases/download/v0.1.2/rules_android_ndk-v0.1.2.tar.gz"],
}

def cpp_network_setup():
    """Bootstrap all external dependencies idempotently."""
    _guard(http_archive, "bazel_skylib", _DEPS["bazel_skylib"])
    _guard(http_archive, "googletest", _DEPS["googletest"])
    _guard(http_archive, "curl", _DEPS["curl"])
    _guard(http_archive, "openssl", _DEPS["openssl"])
    _guard(http_archive, "rules_foreign_cc", _FOREIGN_CC)
    _guard(http_archive, "rules_android_ndk", _RULES_ANDROID_NDK)

