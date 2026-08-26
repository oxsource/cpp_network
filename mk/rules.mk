# AOSP-inspired module registry for the convenience Makefile.
# Mirrors graph_runtime's mk/rules.mk: modules register unique names, targets,
# and aliases; duplicates abort the build instead of silently overriding.

MK_MODULES :=
MK_TARGETS :=
MK_ALIASES :=

# register_module <name> — declare a unique module name.
define register_module
$(if $(filter $1,$(MK_MODULES)),$(error duplicate module: $1))
MK_MODULES += $1
endef

# register_target <target> — register a target owned by the current module.
define register_target
$(if $(filter $1,$(MK_TARGETS)),$(error duplicate target: $1))
MK_TARGETS += $1
endef

# register_alias <alias> <target> — create a short friendly alias for a target.
define register_alias
$(if $(filter $1,$(MK_ALIASES)),$(error duplicate alias: $1))
MK_ALIASES += $1
$1: $2
endef
