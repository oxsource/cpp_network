# Injected BUILD file for the @openssl http_archive (pinned 3.0.13).
#
# Exposes the unpacked upstream tree to the cross-build genrule located in
# //third_party/androidtls (specs/004 research.md D1/D2):
#   * :sources        — every unpacked file (copied into the build sandbox)
#   * :root_entrypoint— single known file used by $(location ...) to derive
#                       the source-root path inside the action sandbox
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sources",
    srcs = glob(["**"]),
)

filegroup(
    name = "root_entrypoint",
    srcs = ["README.md"],
)
