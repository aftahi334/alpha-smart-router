# Microbenchmarks (Google Benchmark)
find_package(benchmark CONFIG REQUIRED)

add_executable(spsc_bench
        ${CMAKE_CURRENT_LIST_DIR}/src/spsc_bench.cpp
)

target_link_libraries(spsc_bench
        PRIVATE
        alpha_core
        benchmark::benchmark
)

target_compile_features(spsc_bench PRIVATE cxx_std_23)
alpha_strict_warnings(spsc_bench)

# Linux often needs pthread for benchmark runtime
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(spsc_bench PRIVATE pthread)
endif()

