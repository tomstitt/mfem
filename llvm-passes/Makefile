# Makefile for RAJA/MFEM Reducer Detection LLVM Pass

CXX := clang++
LLVM_CONFIG := llvm-config

# LLVM flags
LLVM_CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs)

# Compiler flags for the pass
PASS_CXXFLAGS := -fPIC -shared $(LLVM_CXXFLAGS) -fno-rtti
PASS_LDFLAGS := $(LLVM_LDFLAGS)

# Output files
PASS_SO := ReducerDetectionPass.so
TEST_LL := test_reducer.ll
TEST_BC := test_reducer.bc
TEST_BIN := test_reducer

.PHONY: all clean test

all: $(PASS_SO)

# Build the LLVM pass
$(PASS_SO): ReducerDetectionPass.cpp
	$(CXX) $(PASS_CXXFLAGS) $< -o $@ $(PASS_LDFLAGS)
	@echo "Built LLVM pass: $@"

# Compile test to LLVM IR
$(TEST_LL): test_reducer.cpp
	$(CXX) -S -emit-llvm -O1 -g $< -o $@
	@echo "Generated LLVM IR: $@"

# Compile test to bitcode
$(TEST_BC): test_reducer.cpp
	$(CXX) -c -emit-llvm -O1 -g $< -o $@
	@echo "Generated LLVM bitcode: $@"

# Run the pass on the test
test: $(PASS_SO) $(TEST_BC)
	@echo "\n=== Running Reducer Detection Pass on test code ===\n"
	opt -load-pass-plugin=./$(PASS_SO) -passes=reducer-detection -disable-output $(TEST_BC)
	@echo "\n=== Pass complete ===\n"

# Alternative: run on LLVM IR text file
test-ll: $(PASS_SO) $(TEST_LL)
	@echo "\n=== Running Reducer Detection Pass on test code ===\n"
	opt -load-pass-plugin=./$(PASS_SO) -passes=reducer-detection -disable-output $(TEST_LL)
	@echo "\n=== Pass complete ===\n"

# Build and run the actual test binary
run-test: test_reducer.cpp
	$(CXX) -O1 $< -o $(TEST_BIN)
	./$(TEST_BIN)

# View LLVM IR
view-ir: $(TEST_LL)
	less $(TEST_LL)

clean:
	rm -f $(PASS_SO) $(TEST_LL) $(TEST_BC) $(TEST_BIN)

help:
	@echo "RAJA/MFEM Reducer Detection Pass"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the LLVM pass"
	@echo "  test      - Run the pass on test code"
	@echo "  test-ll   - Run on LLVM IR text file"
	@echo "  run-test  - Compile and run the test binary"
	@echo "  view-ir   - View generated LLVM IR"
	@echo "  clean     - Remove generated files"
	@echo "  help      - Show this help"
