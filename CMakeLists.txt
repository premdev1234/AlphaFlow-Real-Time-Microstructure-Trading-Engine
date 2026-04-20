// CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(HFT_Core VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# Project Settings
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release build for performance
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================================
# Header-Only Library Interface
# ============================================================================

add_library(hft_core INTERFACE)

# Add include directory
target_include_directories(hft_core INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Require C++17
target_compile_features(hft_core INTERFACE cxx_std_17)

# ============================================================================
# Compiler Optimizations
# ============================================================================

if(MSVC)
    # MSVC compiler flags
    target_compile_options(hft_core INTERFACE
        /O2                    # Maximize speed
        /Oi                    # Enable intrinsic functions
        /Ot                    # Favor speed over size
        /GL                    # Whole program optimization
        /fp:fast              # Fast floating point
        /arch:AVX2            # AVX2 support (adjust as needed)
    )
    target_link_options(hft_core INTERFACE /LTCG)
else()
    # GCC/Clang compiler flags
    target_compile_options(hft_core INTERFACE
        $<$<CONFIG:Release>:
            -O3                 # Maximum optimization
            -march=native       # Target current CPU
            -mtune=native       # Tune for current CPU
            -ffast-math        # Fast math optimizations
            -flto               # Link-time optimization
            -fno-exceptions    # Disable exceptions if not needed
        >
        $<$<CONFIG:Debug>:
            -g                  # Debug symbols
            -O0                 # No optimization
        >
    )
    
    target_link_options(hft_core INTERFACE
        $<$<CONFIG:Release>:
            -flto               # Link-time optimization
        >
    )
endif()

# ============================================================================
# Enable Testing
# ============================================================================

enable_testing()

# ============================================================================
# Example Executable
# ============================================================================

add_executable(hft_example example.cpp)
target_link_libraries(hft_example PRIVATE hft_core)

# Platform-specific thread linking
if(UNIX AND NOT APPLE)
    target_link_libraries(hft_example PRIVATE pthread)
elseif(WIN32)
    # Windows has built-in threading
endif()

# ============================================================================
# Compiler-Specific Settings
# ============================================================================

if(MSVC)
    # Visual Studio
    target_compile_options(hft_example PRIVATE /W4)  # Warnings
else()
    # GCC/Clang
    target_compile_options(hft_example PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wdouble-promotion
        -Wformat=2
        -Wuninitialized
        -Wfloat-equal
    )
    
    # Linker optimization
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_link_options(hft_example PRIVATE -s)  # Strip symbols
    endif()
endif()

# ============================================================================
# Optional: ASAN (Address Sanitizer) for Debug Builds
# ============================================================================

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
    target_compile_options(hft_example PRIVATE -fsanitize=address)
    target_link_options(hft_example PRIVATE -fsanitize=address)
endif()

# ============================================================================
# Optional: TSAN (Thread Sanitizer)
# ============================================================================

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_TSAN)
    target_compile_options(hft_example PRIVATE -fsanitize=thread)
    target_link_options(hft_example PRIVATE -fsanitize=thread)
endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "======================================")
message(STATUS "HFT Core Module Build Configuration")
message(STATUS "======================================")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Example: hft_example")
message(STATUS "======================================")

# ============================================================================
# Build Targets
# ============================================================================

# Print build instructions
if(NOT MSVC)
    message(STATUS "")
    message(STATUS "Build commands:")
    message(STATUS "  cmake -B build -DCMAKE_BUILD_TYPE=Release")
    message(STATUS "  cmake --build build --config Release")
    message(STATUS "  ./build/hft_example")
    message(STATUS "")
endif()