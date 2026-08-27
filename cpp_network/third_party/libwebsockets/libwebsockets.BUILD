# Injected BUILD file for the @lws http_archive (pinned 4.5.8, specs/006).
#
# :sources enumerates the build-relevant subtrees instead of a recursive
# glob of the whole repo: the tree carries a self-referential symlink
# (minimal-examples/embedded/lhp/esp32-heltec-128-64/libwebsockets -> repo
# root) that makes glob(["**"]) fail with an infinite-symlink-expansion
# error. CMake only needs the entries listed here (lib/, include/, cmake/,
# top-level project files); test apps/examples are disabled by the driver.
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sources",
    srcs = [
        "CMakeLists.txt",
        "CMakeLists-implied-options.txt",
        "component.mk",
    ] + glob(["cmake/**"]) +
          glob(["include/**"]) +
          glob(["lib/**"]),
)

filegroup(
    name = "root_entrypoint",
    srcs = ["README.md"],
)
