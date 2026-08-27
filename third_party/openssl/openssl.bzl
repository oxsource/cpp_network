"""OpenSSL version pinning for the cpp_network workspace.

Consumed by cpp_network_deps.bzl (fetch) and third_party/openssl/BUILD.bazel.
Kept here to centralize version/sha256 in one place per dependency.
"""

OPENSSL_VERSION = "3.0.13"
OPENSSL_SHA256 = "e74504ed7035295ec7062b1da16c15b57ff2a03cd2064a28d8c39458cacc45fc"
OPENSSL_URL = "https://github.com/openssl/openssl/archive/refs/tags/openssl-3.0.13.tar.gz"
OPENSSL_STRIP_PREFIX = "openssl-openssl-3.0.13"
