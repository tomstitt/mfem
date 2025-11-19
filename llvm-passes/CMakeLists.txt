# CMake build for RAJA/MFEM Reducer Detection LLVM Pass

cmake_minimum_required(VERSION 3.13)
project(ReducerDetectionPass)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVM definitions and include directories
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Build the pass as a shared library
add_library(ReducerDetectionPass MODULE ReducerDetectionPass.cpp)

# Link against LLVM
target_link_libraries(ReducerDetectionPass PRIVATE ${LLVM_LIBS})

# Don't add 'lib' prefix on Unix
set_target_properties(ReducerDetectionPass PROPERTIES
  PREFIX ""
  COMPILE_FLAGS "-fno-rtti"
)

# Optional: build test executable
add_executable(test_reducer test_reducer.cpp)

# Custom target to run the pass on the test
add_custom_target(run-pass
  COMMAND ${CMAKE_CXX_COMPILER} -c -emit-llvm -O1 -g
          ${CMAKE_CURRENT_SOURCE_DIR}/test_reducer.cpp
          -o test_reducer.bc
  COMMAND opt -load-pass-plugin=$<TARGET_FILE:ReducerDetectionPass>
          -passes=reducer-detection
          -disable-output test_reducer.bc
  DEPENDS ReducerDetectionPass
  COMMENT "Running Reducer Detection Pass on test code"
  VERBATIM
)

# Help target
add_custom_target(help-pass
  COMMAND ${CMAKE_COMMAND} -E echo "Available targets:"
  COMMAND ${CMAKE_COMMAND} -E echo "  ReducerDetectionPass - Build the LLVM pass"
  COMMAND ${CMAKE_COMMAND} -E echo "  test_reducer        - Build test executable"
  COMMAND ${CMAKE_COMMAND} -E echo "  run-pass            - Run pass on test code"
  VERBATIM
)

# Installation
install(TARGETS ReducerDetectionPass
  LIBRARY DESTINATION lib/llvm-passes
)
