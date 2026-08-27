# Help targets for the convenience Makefile.
# Mirrors graph_runtime's mk/help.mk.

.PHONY: help
help:
	@echo "cpp_network build helpers (via Bazel):"
	@echo "  make build     - bazel build //..."
	@echo "  make test      - bazel test //..."
	@echo "  make verify    - build + test"
	@echo "  make clean     - bazel clean"
	@echo "Android cross-build & deploy (specs/004):"
	@echo "  make build-android - arm64-v8a cpp_network + device binaries"
	@echo "  make push [DEVICE=serial] - artifacts -> /data/local/tmp/cpp_network"
	@echo "  make run  [DEVICE=serial] - remote e2e (default: external HTTPS;"
	@echo "                              RUN_MODE=local: host fixtures + adb reverse)"
	@echo "  make clean-device [DEVICE=serial]"
	@echo "  make verify-android [DEVICE=serial] - one-click cert check"
	@echo "      (build+push+local S1-S7+external E1-E3)"
	@echo "  make menu      - interactive menu"
	@echo "  make modules   - list registered modules"

.PHONY: menu
menu:
	@while true; do \
	  echo; echo "cpp_network menu:"; \
	  echo "  1) build"; \
	  echo "  2) test"; \
	  echo "  3) verify"; \
	  echo "  4) clean"; \
	  echo "  5) quit"; \
	  printf "choice: "; \
	  read -r choice; \
	  case $$choice in \
	    1) $(MAKE) build ;; \
	    2) $(MAKE) test ;; \
	    3) $(MAKE) verify ;; \
	    4) $(MAKE) clean ;; \
	    5) break ;; \
	    *) echo "invalid choice" ;; \
	  esac; \
	done

.PHONY: modules
modules:
	@echo "modules: $(MK_MODULES)"
