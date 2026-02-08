.PHONY: all agent client clean clean-agent clean-client \
       configure configure-agent configure-client \
       init-test-server run-test-server run-test-client-suite \
       test clean-test help

AGENT_BUILD   = agent/_build
CLIENT_BUILD  = clients/cli/_build
AGENT_BIN     = $(AGENT_BUILD)/nabtoshell-agent
CLIENT_BIN    = $(CLIENT_BUILD)/nabtoshell
TEST_DIR      = .test
TEST_AGENT_HOME = $(TEST_DIR)/agent_home
TEST_ENV      = $(TEST_DIR)/env

# ── Build ──────────────────────────────────────────────────────────

all: agent client

agent: $(AGENT_BIN)
$(AGENT_BIN):
	cd agent && cmake -B _build -G Ninja
	cmake --build $(AGENT_BUILD)

client: $(CLIENT_BIN)
$(CLIENT_BIN):
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
	$(AGENT_BIN) --home-dir $(TEST_AGENT_HOME) --init \
		--product-id $(PRODUCT_ID) --device-id $(DEVICE_ID)
	@echo "PRODUCT_ID=$(PRODUCT_ID)" > $(TEST_ENV)
	@echo "DEVICE_ID=$(DEVICE_ID)" >> $(TEST_ENV)
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

# ── Test Client Suite ──────────────────────────────────────────────

run-test-client-suite: $(AGENT_BIN) $(CLIENT_BIN)
	@if [ ! -f "$(TEST_ENV)" ]; then \
		echo "Test server not initialized. Run:"; \
		echo "  make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx"; \
		exit 1; \
	fi
	@. $(TEST_ENV) && \
	AGENT_ABS=$$(cd agent && pwd)/_build/nabtoshell-agent && \
	CLIENT_ABS=$$(cd clients/cli && pwd)/_build/nabtoshell && \
	HOME_ABS=$$(pwd)/$(TEST_AGENT_HOME) && \
	printf '{\n  "product_id": "%s",\n  "device_id": "%s",\n  "agent_binary": "%s",\n  "cli_binary": "%s",\n  "agent_home_dir": "%s",\n  "client_home_dir": "/tmp/nabtoshell-test-client"\n}\n' \
		"$$PRODUCT_ID" "$$DEVICE_ID" "$$AGENT_ABS" "$$CLIENT_ABS" "$$HOME_ABS" \
		> tests/test_config.json
	python3 -m pytest tests/ -v

# ── Help ───────────────────────────────────────────────────────────

test:
	@echo "Testing requires a running agent with Nabto Cloud credentials."
	@echo ""
	@echo "  1. make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx"
	@echo "     Register the printed fingerprint in the Nabto Cloud Console."
	@echo ""
	@echo "  2. make run-test-server          (Terminal 1, runs in foreground)"
	@echo ""
	@echo "  3. make run-test-client-suite    (Terminal 2)"
	@echo ""
	@echo "  Cleanup: make clean-test"

help:
	@echo "NabtoShell Makefile"
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
	@echo "  make run-test-client-suite    Run pytest suite against running server"
