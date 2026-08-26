"""Idempotent external dependency bootstrap for the cpp_network workspace.

Mirrors graph_runtime's `graph_runtime_deps.bzl` pattern: every dependency is
guarded by `native.existing_rule(...)` so that repeated calls to `netlib_setup()`
are no-ops.

All sha256 values are real checksums of the pinned archives (verified).
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

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
        "sha256": "0e46c856f517602c347bb5fe5b73174f8ee798bc87f1a97235c95761f75fcc28",
        "urls": ["https://github.com/curl/curl/archive/refs/tags/curl-8_7_1.tar.gz"],
        "strip_prefix": "curl-curl-8_7_1",
    },
    "openssl": {
        # OpenSSL 3.x LTS. Pinned 3.0.13. Used as the TLS backend on ALL
        # platforms (host macOS/Linux and Android).
        "sha256": "e74504ed7035295ec7062b1da16c15b57ff2a03cd2064a28d8c39458cacc45fc",
        "urls": ["https://github.com/openssl/openssl/archive/refs/tags/openssl-3.0.13.tar.gz"],
        "strip_prefix": "openssl-openssl-3.0.13",
    },
}

def _guard(rule, name, kwargs):
    if native.existing_rule(name):
        return
    rule(name = name, **kwargs)

def netlib_setup():
    """Bootstrap all external dependencies idempotently."""
    _guard(http_archive, "bazel_skylib", _DEPS["bazel_skylib"])
    _guard(http_archive, "googletest", _DEPS["googletest"])
    _guard(http_archive, "curl", _DEPS["curl"])
    _guard(http_archive, "openssl", _DEPS["openssl"])

