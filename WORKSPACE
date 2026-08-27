workspace(name = "cpp_network")

load("//:cpp_network_deps.bzl", "cpp_network_setup")

cpp_network_setup()

# Foreign-cc repositories provide make/cmake/ninja integration used by
# third_party/openssl and third_party/libcurl on the Android config.
# register_built_tools=False relies on preinstalled host make/pkg-config,
# mirroring the verified video_codec setup (specs/004 research.md D2/D3).
load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies(
    register_built_tools = False,
    register_built_pkgconfig_toolchain = False,
)

# Android NDK repo via rules_android_ndk (research.md D3): fetched lazily;
# its cc_toolchain is registered only for android configs through .bazelrc
# (--extra_toolchains), so host (non-Android) builds never require the NDK.
load("@rules_android_ndk//:rules.bzl", "android_ndk_repository")

android_ndk_repository(name = "androidndk")
