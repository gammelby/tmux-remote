.PHONY: all agent client clean clean-agent clean-client \
       configure configure-agent configure-client \
       init-test-server run-test-server setup-test-client \
       run-tests-offline run-tests-online run-test-client-suite \
       test clean-test help

PYTHON        ?= python3
AGENT_BUILD   = agent/_build
CLIENT_BUILD  = clients/cli/_build
AGENT_BIN     = $(AGENT_BUILD)/tmux-remote-agent
CLIENT_BIN    = $(CLIENT_BUILD)/tmux-remote
TEST_DIR      = .test
TEST_AGENT_HOME = $(TEST_DIR)/agent_home
TEST_CLIENT_HOME = $(TEST_DIR)/client_home
TEST_ENV      = $(TEST_DIR)/env

# ── Build ──────────────────────────────────────────────────────────

all: agent client

agent:
	cd agent && cmake -B _build -G Ninja
	cmake --build $(AGENT_BUILD)

client:
	cd clients/cli && cmake -B _build -G Ninja
	cmake --build $(CLIENT_BUILD)

configure: configure-agent configure-client

configure-agent:
	cd agent && cmake -B _build -G Ninja

configure-client:
	cd clients/cli && cmake -B _build -G Ninja

# ── Clean ──────────────────────────────────────────────────────────

clean: clean-agent clean-client

clean-agent:
	rm -rf $(AGENT_BUILD)

clean-client:
	rm -rf $(CLIENT_BUILD)

clean-test:
	rm -rf $(TEST_DIR)
	rm -f tests/test_config.json

# ── Test Server ────────────────────────────────────────────────────

init-test-server: $(AGENT_BIN)
ifndef PRODUCT_ID
	$(error PRODUCT_ID is required. Usage: make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx)
endif
ifndef DEVICE_ID
	$(error DEVICE_ID is required. Usage: make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx)
endif
	@mkdir -p $(TEST_DIR)
	@if [ -f "$(TEST_AGENT_HOME)/config/device.json" ]; then \
		echo "Test server already initialized in $(TEST_AGENT_HOME)/."; \
		echo "Run 'make clean-test' first to reinitialize."; \
		exit 1; \
	fi
	@$(AGENT_BIN) --home-dir $(TEST_AGENT_HOME) --init \
		--product-id $(PRODUCT_ID) --device-id $(DEVICE_ID) \
		| tee $(TEST_DIR)/init_output.txt
	@echo "PRODUCT_ID=$(PRODUCT_ID)" > $(TEST_ENV)
	@echo "DEVICE_ID=$(DEVICE_ID)" >> $(TEST_ENV)
	@PAIR=$$(grep -oE 'p=[^ ]+' $(TEST_DIR)/init_output.txt | head -1) && \
		if [ -n "$$PAIR" ]; then \
			echo "PAIRING_STRING=$$PAIR" >> $(TEST_ENV); \
		fi
	@rm -f $(TEST_DIR)/init_output.txt
	@echo ""
	@echo "Register the fingerprint above in the Nabto Cloud Console,"
	@echo "then run:  make run-test-server"

run-test-server: $(AGENT_BIN)
	@if [ ! -f "$(TEST_AGENT_HOME)/config/device.json" ]; then \
		echo "Test server not initialized. Run:"; \
		echo "  make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx"; \
		exit 1; \
	fi
	$(AGENT_BIN) --home-dir $(TEST_AGENT_HOME) --log-level info --random-ports

# ── Test Client Setup ─────────────────────────────────────────────

setup-test-client: $(CLIENT_BIN)
	@if [ ! -f "$(TEST_ENV)" ]; then \
		echo "Test server not initialized. Run:"; \
		echo "  make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx"; \
		exit 1; \
	fi
	@. $(TEST_ENV) && \
	if [ -z "$$PAIRING_STRING" ]; then \
		echo "No PAIRING_STRING in $(TEST_ENV)."; \
		echo "Re-run: make clean-test && make init-test-server ..."; \
		exit 1; \
	fi && \
	CLIENT_ABS=$$(cd clients/cli && pwd)/_build/tmux-remote && \
	mkdir -p $(TEST_CLIENT_HOME) && \
	echo "Pairing client with test server..." && \
	TMUX_REMOTE_HOME=$(TEST_CLIENT_HOME) $$CLIENT_ABS pair $$PAIRING_STRING --name default && \
	echo "" && \
	echo "Client paired successfully. State saved to $(TEST_CLIENT_HOME)/" && \
	echo "Run:  make run-tests-online"

# ── Test Suites ───────────────────────────────────────────────────

define generate_test_config
	@. $(TEST_ENV) && \
	AGENT_ABS=$$(cd agent && pwd)/_build/tmux-remote-agent && \
	CLIENT_ABS=$$(cd clients/cli && pwd)/_build/tmux-remote && \
	HOME_ABS=$$(pwd)/$(TEST_AGENT_HOME) && \
	CLIENT_HOME_ABS=$$(pwd)/$(TEST_CLIENT_HOME) && \
	printf '{\n  "product_id": "%s",\n  "device_id": "%s",\n  "agent_binary": "%s",\n  "cli_binary": "%s",\n  "agent_home_dir": "%s",\n  "client_home_dir": "%s"\n}\n' \
		"$$PRODUCT_ID" "$$DEVICE_ID" "$$AGENT_ABS" "$$CLIENT_ABS" "$$HOME_ABS" "$$CLIENT_HOME_ABS" \
		> tests/test_config.json
endef

run-tests-offline: $(AGENT_BIN) $(CLIENT_BIN)
	@if [ ! -f "$(TEST_ENV)" ]; then \
		echo "Test server not initialized. Run:"; \
		echo "  make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx"; \
		exit 1; \
	fi
	$(generate_test_config)
	$(PYTHON) -m pytest tests/offline/ -v

run-tests-online: $(AGENT_BIN) $(CLIENT_BIN)
	@if [ ! -f "$(TEST_ENV)" ]; then \
		echo "Test server not initialized. Run:"; \
		echo "  make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx"; \
		exit 1; \
	fi
	@if [ ! -d "$(TEST_CLIENT_HOME)" ]; then \
		echo "Test client not set up. Run (with test server running):"; \
		echo "  make setup-test-client"; \
		exit 1; \
	fi
	$(generate_test_config)
	$(PYTHON) -m pytest tests/online/ -v

run-test-client-suite: $(AGENT_BIN) $(CLIENT_BIN)
	@if [ ! -f "$(TEST_ENV)" ]; then \
		echo "Test server not initialized. Run:"; \
		echo "  make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx"; \
		exit 1; \
	fi
	$(generate_test_config)
	@echo "=== Offline tests (no server needed) ==="
	$(PYTHON) -m pytest tests/offline/ -v
	@if [ -d "$(TEST_CLIENT_HOME)" ]; then \
		echo ""; \
		echo "=== Online tests (against running server) ==="; \
		$(PYTHON) -m pytest tests/online/ -v; \
	else \
		echo ""; \
		echo "Skipping online tests: no pre-paired client."; \
		echo "To run online tests, start the server and run: make setup-test-client"; \
	fi

# ── Help ───────────────────────────────────────────────────────────

test:
	@echo "Testing requires a running agent with Nabto Cloud credentials."
	@echo ""
	@echo "  1. make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx"
	@echo "     Register the printed fingerprint in the Nabto Cloud Console."
	@echo ""
	@echo "  2. make run-test-server          (Terminal 1, runs in foreground)"
	@echo ""
	@echo "  3. make setup-test-client        (Terminal 2, pairs client once)"
	@echo ""
	@echo "  4. make run-tests-offline        (no server needed)"
	@echo "     make run-tests-online         (requires running server)"
	@echo "     make run-test-client-suite    (runs both sequentially)"
	@echo ""
	@echo "  Cleanup: make clean-test"

help:
	@echo "tmux-remote Makefile"
	@echo ""
	@echo "Build:"
	@echo "  make                          Build both agent and client"
	@echo "  make agent                    Build agent only"
	@echo "  make client                   Build CLI client only"
	@echo "  make configure                Run cmake configure for both"
	@echo ""
	@echo "Clean:"
	@echo "  make clean                    Remove all build artifacts"
	@echo "  make clean-agent              Remove agent build artifacts"
	@echo "  make clean-client             Remove client build artifacts"
	@echo "  make clean-test               Remove test server state"
	@echo ""
	@echo "Test:"
	@echo "  make test                     Show test workflow instructions"
	@echo "  make init-test-server         Initialize test server (needs PRODUCT_ID, DEVICE_ID)"
	@echo "  make run-test-server          Start test server in foreground"
	@echo "  make setup-test-client        Pair client with running test server (once)"
	@echo "  make run-tests-offline        Run offline tests (no server needed)"
	@echo "  make run-tests-online         Run online tests (needs server + paired client)"
	@echo "  make run-test-client-suite    Run both test suites sequentially"
