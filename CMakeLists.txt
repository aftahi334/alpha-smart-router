cmake_minimum_required(VERSION 3.22)
project(alpha_smart_router LANGUAGES CXX)

# ---- Options (toggle features from CLI/CI) -----------------------------------
option(ALPHA_ENABLE_TESTS       "Build unit tests" ON)
option(ALPHA_ENABLE_BENCHMARKS  "Build benchmarks" ON)
option(ALPHA_ENABLE_LTO         "Enable link-time optimization (Release only)" OFF)
option(ALPHA_STRICT_WARNINGS    "Enable strict compiler warnings" ON)

# ---- Enable CTest / test registration ----------------------------------------
include(CTest)  # defines BUILD_TESTING and calls enable_testing() when ON

# ---- Global C++ settings -----------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build type (can still override via -DCMAKE_BUILD_TYPE=...)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- Strict warnings helper -------------------------------------------------
function(alpha_strict_warnings target)
    if(ALPHA_STRICT_WARNINGS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive- /wd4996)
        else()
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic
                -Wconversion -Wsign-conversion
                -Wshadow -Wmissing-field-initializers
            )
        endif()
    endif()
endfunction()


# Optional IPO/LTO (use only where toolchain supports it)
if(ALPHA_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_msg)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else()
        message(WARNING "IPO/LTO not supported: ${ipo_msg}")
    endif()
endif()

# ---- Build tree --------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(apps/router_app)
add_subdirectory(apps/ping_tool)

if(BUILD_TESTING AND ALPHA_ENABLE_TESTS)
  add_subdirectory(tests)
endif()

if(ALPHA_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
