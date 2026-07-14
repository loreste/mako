# Mako — local install helpers
#   make install   → PREFIX=$(HOME)/.local (binary + share/mako/runtime headers)
#   make test      → examples/testing

PREFIX ?= $(HOME)/.local
BIN_DIR ?= $(PREFIX)/bin
SHARE_DIR ?= $(PREFIX)/share/mako
RUNTIME_DST ?= $(SHARE_DIR)/runtime
CARGO ?= cargo
TARGET_DIR ?= $(shell $(CARGO) metadata --format-version 1 --no-deps 2>/dev/null | python3 -c 'import json,sys; print(json.load(sys.stdin)["target_directory"])' 2>/dev/null || echo target)
MAKO_BIN := $(TARGET_DIR)/release/mako

.PHONY: all release build install uninstall test help clean version

all: release

# Product version is MAKO_VERSION in src/main.rs (e.g. 0.0.1.2); bake git hash when available.
MAKO_GIT_HASH ?= $(shell git rev-parse --short HEAD 2>/dev/null)

release:
	MAKO_GIT_HASH="$(MAKO_GIT_HASH)" $(CARGO) build --release

build: release

install: release
	mkdir -p "$(BIN_DIR)" "$(RUNTIME_DST)/certs" "$(RUNTIME_DST)/third_party"
	install -m 755 "$(MAKO_BIN)" "$(BIN_DIR)/mako"
	install -m 644 runtime/*.h "$(RUNTIME_DST)/"
	@if [ -d runtime/certs ]; then cp -R runtime/certs/. "$(RUNTIME_DST)/certs/"; fi
	@if [ -f runtime/third_party/README.md ]; then \
	  install -m 644 runtime/third_party/README.md "$(RUNTIME_DST)/third_party/"; \
	fi
	@echo "Installed $(BIN_DIR)/mako"
	@echo "Installed $(RUNTIME_DST)"
	@"$(BIN_DIR)/mako" version -v
	@echo "Optional: export MAKO_RUNTIME=$(RUNTIME_DST)"

uninstall:
	rm -f "$(BIN_DIR)/mako"
	rm -rf "$(SHARE_DIR)"

test:
	$(CARGO) run --quiet --release -- test examples/testing

version:
	$(CARGO) run --quiet --release -- --version

help:
	@echo "Targets: release install uninstall test version help"
	@echo "PREFIX=$(PREFIX)  RUNTIME_DST=$(RUNTIME_DST)"

clean:
	$(CARGO) clean
