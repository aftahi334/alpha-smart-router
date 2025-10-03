# apps/ping_tool/CMakeLists.txt

# Standalone helper to probe RTT/jitter/loss (demo/testing)
add_executable(ping_tool
        src/main.cpp
)

# Link against the core library
target_link_libraries(ping_tool PRIVATE alpha_core)

# C++ standard & warnings
target_compile_features(ping_tool PRIVATE cxx_std_20)
alpha_strict_warnings(ping_tool)

# Portable pthread/threads linkage (Linux, QNX, etc.)
find_package(Threads REQUIRED)
target_link_libraries(ping_tool PRIVATE Threads::Threads)

# Optional install
install(TARGETS ping_tool RUNTIME DESTINATION bin)
