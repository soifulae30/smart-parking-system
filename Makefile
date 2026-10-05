BUILD    ?= build
SAN      ?=
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -g -pthread -Iinclude $(SAN)
LDFLAGS  := -pthread $(SAN)

SRCS      := $(wildcard src/*.cpp)
OBJS      := $(SRCS:src/%.cpp=$(BUILD)/%.o)
LIB_OBJS  := $(filter-out $(BUILD)/main.o,$(OBJS))
TARGET    := $(BUILD)/smart_parking

TEST_SRCS := $(wildcard tests/*.cpp)
TEST_OBJS := $(TEST_SRCS:tests/%.cpp=$(BUILD)/tests/%.o)
TEST_BIN  := $(BUILD)/run_tests

.PHONY: all run test demo check clean tsan test-tsan test-asan

all: $(TARGET)

# Link the application
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# Link the test program: every module EXCEPT main.cpp, plus the test files
$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Run all tests. `timeout` ends the run if a deadlock ever hangs a test.
test: $(TEST_BIN)
	timeout 120 ./$(TEST_BIN)

# Scripted demonstration against a real server (needs curl)
demo: all
	bash scripts/demo.sh

# Delivery check: files, docs, clean build, full test run
check:
	bash scripts/check_delivery.sh --full

# Compile: one .cpp -> one .o (-MMD -MP records header dependencies)
$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/tests/%.o: tests/%.cpp | $(BUILD)/tests
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD) $(BUILD)/tests:
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

# Separate build directories so sanitized and normal objects never mix
tsan:
	$(MAKE) BUILD=build_tsan SAN="-fsanitize=thread -O1" all

test-tsan:
	$(MAKE) BUILD=build_tsan SAN="-fsanitize=thread -O1" test

test-asan:
	$(MAKE) BUILD=build_asan SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -O1" test

clean:
	rm -rf build build_tsan build_asan

-include $(wildcard $(BUILD)/*.d $(BUILD)/tests/*.d)