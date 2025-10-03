# apps/router_app/CMakeLists.txt

# Router demo entrypoint (Ingress → latency-aware backbone)
add_executable(router_app
        src/main.cpp
)

# Link against the core library
target_link_libraries(router_app PRIVATE alpha_core)

# Optional: config parsing via tomlplusplus if available
find_package(tomlplusplus CONFIG QUIET)
if(tomlplusplus_FOUND)
    target_link_libraries(router_app PRIVATE tomlplusplus::tomlplusplus)
endif()

# C++ standard & warnings (keep app-local; alpha_core publishes include/)
target_compile_features(router_app PRIVATE cxx_std_20)
alpha_strict_warnings(router_app)

# Portable pthread/threads linkage (Linux, QNX, etc.)
find_package(Threads REQUIRED)
target_link_libraries(router_app PRIVATE Threads::Threads)

# Optional install for packaging/demos
install(TARGETS router_app RUNTIME DESTINATION bin)
