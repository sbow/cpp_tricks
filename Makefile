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

.PHONY: all stages clean debug help run $(PROGRAMS) $(foreach p,$(PROGRAMS),stages-$(p) run-$(p))

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
	@echo "  make clean        remove build/<program>/ for each program"
