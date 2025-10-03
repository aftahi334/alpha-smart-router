set(ALPHA_SRC ${CMAKE_SOURCE_DIR}/src/alpha)

# Core library that apps/tests/bench link against.
add_library(alpha_core
        ${ALPHA_SRC}/mem/packet_pool.cpp
        ${ALPHA_SRC}/mem/spsc_queue.cpp
        ${ALPHA_SRC}/mem/mem_primitives.cpp
        ${ALPHA_SRC}/routing/path_selection.cpp
        ${ALPHA_SRC}/routing/policy_binding.cpp
        ${ALPHA_SRC}/routing/qos_policy.cpp
        ${ALPHA_SRC}/routing/failover_policy.cpp
        ${ALPHA_SRC}/routing/ingress_selector.cpp
        ${ALPHA_SRC}/routing/service_registry.cpp
        ${ALPHA_SRC}/routing/bgp_oracle_sim.cpp
        ${ALPHA_SRC}/config/config_loader.cpp
        ${ALPHA_SRC}/obs/observability.cpp
)

# Public headers for dependents (apps/tests)
target_include_directories(alpha_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Language + warnings; PIC for flexible linking
target_compile_features(alpha_core PUBLIC cxx_std_20)
alpha_strict_warnings(alpha_core)
set_target_properties(alpha_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Optional dependency: toml++ (header-only). OK if missing.
find_package(tomlplusplus CONFIG QUIET)
if(tomlplusplus_FOUND)
    target_link_libraries(alpha_core PUBLIC tomlplusplus::tomlplusplus)
    target_compile_definitions(alpha_core PUBLIC ALPHA_HAS_TOMLPLUSPLUS=1)
else()
    message(STATUS "toml++ not found (config loading can be added later).")
endif()
