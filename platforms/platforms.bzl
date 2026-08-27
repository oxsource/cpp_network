"""Platform helper macros for the cpp_network Bazel workspace.

Mirrors the graph_runtime project's `platforms/platforms.bzl` conventions.
"""

def config_setting_and_platform(name, constraint_values, parents = None):
    """Create a paired config_setting and platform target.

    Args:
        name: Base name; produces <name>_setting (config_setting) and <name> (platform).
        constraint_values: List of constraint values identifying the platform.
        parents: Optional list of parent platform labels.
    """
    native.config_setting(
        name = name + "_setting",
        constraint_values = constraint_values,
    )
    native.platform(
        name = name,
        constraint_values = constraint_values,
        parents = parents,
    )
