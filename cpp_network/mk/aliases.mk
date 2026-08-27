# Convenience aliases for common Bazel operations.
# Mirrors graph_runtime's mk/aliases.mk.

.PHONY: build
build:
	bazel build //...

.PHONY: test
test:
	bazel test //...

.PHONY: verify
verify: build test
	@echo "[verify] build + test succeeded"

.PHONY: clean
clean:
	bazel clean
