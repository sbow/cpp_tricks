# Compiler and flags
CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra
LDFLAGS  :=

PROGRAMS_ROOT := cpp_tricks
BUILD_ROOT    := build

# Programs with a src/ tree containing at least one .cpp file
PROGRAMS := $(sort $(foreach d,$(wildcard $(PROGRAMS_ROOT)/*/src),$(notdir $(patsubst %/,%,$(dir $(d))))))
PROGRAMS := $(foreach p,$(PROGRAMS),$(if $(wildcard $(PROGRAMS_ROOT)/$(p)/src/*.cpp),$(p),))

TARGETS := $(foreach p,$(PROGRAMS),$(BUILD_ROOT)/$(p)/$(p))

.PHONY: all stages clean debug help run test-ipc test-ipc-mp test-router ipc-echo-server ipc-echo-client ipc-echo-client-benchmark ipc-router-server ipc-router-client $(PROGRAMS) $(foreach p,$(PROGRAMS),stages-$(p) run-$(p))

.DEFAULT_GOAL := all

all: $(TARGETS)

# Per-program variables, rules, and phony targets
define PROGRAM_template
$(1)_SRCS := $$(wildcard $(PROGRAMS_ROOT)/$(1)/src/*.cpp)
$(1)_OBJS := $$(patsubst $(PROGRAMS_ROOT)/$(1)/src/%.cpp,$(BUILD_ROOT)/$(1)/%.o,$$($(1)_SRCS))
$(1)_PREPROCESSED := $$(patsubst $(PROGRAMS_ROOT)/$(1)/src/%.cpp,$(BUILD_ROOT)/$(1)/%.i,$$($(1)_SRCS))
$(1)_ASSEMBLY := $$(patsubst $(PROGRAMS_ROOT)/$(1)/src/%.cpp,$(BUILD_ROOT)/$(1)/%.s,$$($(1)_SRCS))
$(1)_TARGET := $(BUILD_ROOT)/$(1)/$(1)

$(1): $$($(1)_TARGET)

stages-$(1): $$($(1)_PREPROCESSED) $$($(1)_ASSEMBLY) $$($(1)_OBJS) $$($(1)_TARGET)

run-$(1): $$($(1)_TARGET)
	./$$($(1)_TARGET)

$(BUILD_ROOT)/$(1):
	mkdir -p $$@

$(BUILD_ROOT)/$(1)/$(1): $$($(1)_OBJS) | $(BUILD_ROOT)/$(1)
	$$(CXX) $$(CXXFLAGS) $$(LDFLAGS) -o $$@ $$^

$(BUILD_ROOT)/$(1)/%.o: $(PROGRAMS_ROOT)/$(1)/src/%.cpp | $(BUILD_ROOT)/$(1)
	$$(CXX) $$(CXXFLAGS) -c -o $$@ $$<

$(BUILD_ROOT)/$(1)/%.i: $(PROGRAMS_ROOT)/$(1)/src/%.cpp | $(BUILD_ROOT)/$(1)
	$$(CXX) $$(CXXFLAGS) -E -o $$@ $$<

$(BUILD_ROOT)/$(1)/%.s: $(PROGRAMS_ROOT)/$(1)/src/%.cpp | $(BUILD_ROOT)/$(1)
	$$(CXX) $$(CXXFLAGS) -S -o $$@ $$<
endef

$(foreach p,$(PROGRAMS),$(eval $(call PROGRAM_template,$(p))))

# Build every intermediate artifact for every program
stages: $(foreach p,$(PROGRAMS),stages-$(p))

# Run the first discovered program (basic, when present)
run: run-$(firstword $(PROGRAMS))

debug: CXXFLAGS += -g -O0
debug: clean all

clean:
	rm -rf $(foreach p,$(PROGRAMS),$(BUILD_ROOT)/$(p))
	rm -rf $(BUILD_ROOT)/ipc/test

IPC_TEST_DIR     := $(BUILD_ROOT)/ipc/test
IPC_TEST_FLAGS   := -Icpp_tricks/ipc/src -pthread
IPC_ECHO_TEST    := $(IPC_TEST_DIR)/echo_tests
IPC_ECHO_SERVER           := $(IPC_TEST_DIR)/echo_server
IPC_ECHO_CLIENT           := $(IPC_TEST_DIR)/echo_client
IPC_ECHO_CLIENT_BENCHMARK := $(IPC_TEST_DIR)/echo_client_benchmark
IPC_ROUTER_SERVER         := $(IPC_TEST_DIR)/router_server
IPC_ROUTER_CLIENT         := $(IPC_TEST_DIR)/router_client
IPC_ROUTER_TEST           := $(IPC_TEST_DIR)/router_test
IPC_UDP_PORT     := 19000
IPC_UDS_SERVER   := /tmp/cpp_tricks_echo_server.sock
IPC_UDS_CLIENT   := /tmp/cpp_tricks_echo_client.sock
IPC_TEST_SECONDS := 5

$(IPC_TEST_DIR):
	mkdir -p $@

$(IPC_ECHO_TEST): cpp_tricks/ipc/test/echo_tests.cpp cpp_tricks/ipc/src/ipc.h | $(IPC_TEST_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(IPC_TEST_FLAGS) -o $@ $<

$(IPC_ECHO_SERVER): cpp_tricks/ipc/test/echo_server.cpp cpp_tricks/ipc/src/ipc.h | $(IPC_TEST_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(IPC_TEST_FLAGS) -o $@ $<

$(IPC_ECHO_CLIENT): cpp_tricks/ipc/test/echo_client.cpp cpp_tricks/ipc/src/ipc.h | $(IPC_TEST_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(IPC_TEST_FLAGS) -o $@ $<

$(IPC_ECHO_CLIENT_BENCHMARK): cpp_tricks/ipc/test/echo_client_benchmark.cpp cpp_tricks/ipc/src/ipc.h | $(IPC_TEST_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(IPC_TEST_FLAGS) -o $@ $<

$(IPC_ROUTER_SERVER): cpp_tricks/ipc/test/router_server.cpp cpp_tricks/ipc/src/ipc.h cpp_tricks/ipc/src/router_protocol.h cpp_tricks/ipc/src/router_app.h cpp_tricks/ipc/test/router_client_config.h | $(IPC_TEST_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(IPC_TEST_FLAGS) -o $@ $<

$(IPC_ROUTER_CLIENT): cpp_tricks/ipc/test/router_client.cpp cpp_tricks/ipc/src/ipc.h cpp_tricks/ipc/src/router_protocol.h cpp_tricks/ipc/src/router_app.h cpp_tricks/ipc/test/router_client_config.h | $(IPC_TEST_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(IPC_TEST_FLAGS) -o $@ $<

$(IPC_ROUTER_TEST): cpp_tricks/ipc/test/router_test.cpp cpp_tricks/ipc/src/router_protocol.h cpp_tricks/ipc/test/router_client_config.h | $(IPC_TEST_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(IPC_TEST_FLAGS) -o $@ $<

ipc-echo-server: $(IPC_ECHO_SERVER)
ipc-echo-client: $(IPC_ECHO_CLIENT)
ipc-echo-client-benchmark: $(IPC_ECHO_CLIENT_BENCHMARK)
ipc-router-server: $(IPC_ROUTER_SERVER)
ipc-router-client: $(IPC_ROUTER_CLIENT)

test-ipc: $(IPC_ECHO_TEST)
	./$(IPC_ECHO_TEST)

test-router: $(IPC_ROUTER_TEST) $(IPC_ROUTER_SERVER) $(IPC_ROUTER_CLIENT)
	./$(IPC_ROUTER_TEST)

test-ipc-mp: $(IPC_ECHO_SERVER) $(IPC_ECHO_CLIENT_BENCHMARK)
	$(IPC_ECHO_SERVER) udp $(IPC_UDP_PORT) & \
	server_pid=$$!; \
	sleep 0.2; \
	$(IPC_ECHO_CLIENT_BENCHMARK) udp 127.0.0.1 $(IPC_UDP_PORT) $(IPC_TEST_SECONDS); \
	kill $$server_pid 2>/dev/null; wait $$server_pid 2>/dev/null || true
	rm -f $(IPC_UDS_SERVER) $(IPC_UDS_CLIENT); \
	$(IPC_ECHO_SERVER) uds $(IPC_UDS_SERVER) & \
	server_pid=$$!; \
	sleep 0.2; \
	$(IPC_ECHO_CLIENT_BENCHMARK) uds $(IPC_UDS_SERVER) $(IPC_UDS_CLIENT) $(IPC_TEST_SECONDS); \
	kill $$server_pid 2>/dev/null; wait $$server_pid 2>/dev/null || true; \
	rm -f $(IPC_UDS_SERVER) $(IPC_UDS_CLIENT)

help:
	@echo "Programs: $(if $(PROGRAMS),$(PROGRAMS),(none — add cpp_tricks/<name>/src/*.cpp))"
	@echo ""
	@echo "  make [all]        build all programs"
	@echo "  make <program>    build one program (e.g. make basic)"
	@echo "  make run-<program>  build and run (e.g. make run-basic)"
	@echo "  make run          build and run the first program"
	@echo "  make stages       build .i / .s / .o / binary for all programs"
	@echo "  make stages-<program>  intermediates for one program"
	@echo "  make debug        rebuild all with -g -O0"
	@echo "  make test-ipc         build and run in-process ipc echo benchmark"
	@echo "  make test-ipc-mp      build and run two-process udp + uds echo benchmark"
	@echo "  make ipc-echo-server  build build/ipc/test/echo_server"
	@echo "  make ipc-echo-client  build build/ipc/test/echo_client"
	@echo "  make test-router       build and run udp + uds router scenario test"
	@echo "  make ipc-router-server build build/ipc/test/router_server"
	@echo "  make ipc-router-client build build/ipc/test/router_client"
	@echo "  make clean        remove build/<program>/ for each program"
