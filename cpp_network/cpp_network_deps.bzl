"""Idempotent external dependency bootstrap for the cpp_network workspace.

This file is the SINGLE SOURCE OF TRUTH for every external dependency pin
(version, sha256, URL). All sha256 values are real checksums of the pinned
archives (verified per spec 002 SC-006).

Every dependency is guarded by `native.existing_rule(...)` so that repeated
calls to `cpp_network_setup()` are no-ops.

`build_file` labels are repo-qualified (@cpp_network//...) so the macro is also
safe to call from a consumer workspace that pulls cpp_network in via
local_repository (e.g. the video_stream module).
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Single ordered registry: name is a first-class field; each entry carries its
# http_archive kwargs verbatim. Keep entries alphabetically sorted and give
# each one a one-line purpose comment above it.
_CPP_NETWORK_DEPS = [
    # libcurl pinned to the official release tarball, which ships a
    # pre-generated ./configure — GitHub source archives require autoreconf,
    # which we do not run in the sandbox. Consumed by //third_party/openssl/android
    # on the android config only.
    {
        "name": "curl",
        "sha256": "f91249c87f68ea00cf27c44fdfa5a78423e41e71b7d408e5901a9896d905c495",
        "strip_prefix": "curl-8.7.1",
        "urls": ["https://curl.se/download/curl-8.7.1.tar.gz"],
        "build_file": "@cpp_network//third_party/libcurl:curl_external.BUILD",
    },
    # GoogleTest for the host test suites.
    {
        "name": "googletest",
        "sha256": "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
        "strip_prefix": "googletest-1.14.0",
        "urls": ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
    },
    # libwebsockets v4.5.8 stable — WebSocket client transport (specs/006),
    # CMake-built against the TLS bundle's OpenSSL slice (single TLS stack).
    {
        "name": "lws",
        "sha256": "b6ade658f4af3a823d0dc806ae5ef0623f0f4f5e2aeb895a0f77c4783840c30e",
        "strip_prefix": "libwebsockets-4.5.8",
        "urls": ["https://github.com/warmcat/libwebsockets/archive/refs/tags/v4.5.8.tar.gz"],
        "build_file": "@cpp_network//third_party/libwebsockets:libwebsockets.BUILD",
    },
    # OpenSSL 3.x LTS — TLS backend cross-built from source on the android
    # config (specs/004 research.md D1); other configs link the system curl
    # TLS stack instead.
    {
        "name": "openssl",
        "sha256": "e74504ed7035295ec7062b1da16c15b57ff2a03cd2064a28d8c39458cacc45fc",
        "strip_prefix": "openssl-openssl-3.0.13",
        "urls": ["https://github.com/openssl/openssl/archive/refs/tags/openssl-3.0.13.tar.gz"],
        "build_file": "@cpp_network//third_party/openssl:openssl.BUILD",
    },
    # rules_android_ndk v0.1.2 provides a modern-layout android_ndk_repository
    # that is fetched lazily (research.md D3): hosts without ANDROID_NDK_HOME
    # never touch it.
    {
        "name": "rules_android_ndk",
        "sha256": "65aedff0cd728bee394f6fb8e65ba39c4c5efb11b29b766356922d4a74c623f5",
        "strip_prefix": "rules_android_ndk-0.1.2",
        "urls": ["https://github.com/bazelbuild/rules_android_ndk/releases/download/v0.1.2/rules_android_ndk-v0.1.2.tar.gz"],
    },
    # rules_foreign_cc hosts configure/make style builds. Pinned 0.10.1:
    # validated against Bazel 6.5; upgrades require re-validating the
    # third_party cross-compilation chain.
    {
        "name": "rules_foreign_cc",
        "sha256": "476303bd0f1b04cc311fc258f1708a5f6ef82d3091e53fd1977fa20383425a6a",
        "strip_prefix": "rules_foreign_cc-0.10.1",
        "urls": ["https://github.com/bazel-contrib/rules_foreign_cc/archive/refs/tags/0.10.1.tar.gz"],
    },
]

def _guard(rule, name, kwargs):
    """Registers `rule` unless an identical-name rule already exists."""
    if native.existing_rule(name):
        return
    rule(name = name, **kwargs)

def cpp_network_setup():
    """Bootstrap all external dependencies idempotently."""
    for dep in _CPP_NETWORK_DEPS:
        kwargs = {key: value for key, value in dep.items() if key != "name"}
        _guard(http_archive, dep["name"], kwargs)
