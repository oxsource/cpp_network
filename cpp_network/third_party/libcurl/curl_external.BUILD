# Injected BUILD file for the @curl http_archive (pinned 8.7.1).
#
# Mirrors third_party/openssl/openssl.BUILD: :sources feeds the
# androidtls cross-build genrule; :root_entrypoint anchors $(location ...).
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sources",
    srcs = glob(["**"]),
)

filegroup(
    name = "root_entrypoint",
    srcs = ["configure"],
)
