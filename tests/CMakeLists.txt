# Unit tests (via Conan or system packages)
find_package(GTest CONFIG REQUIRED)

add_executable(test_routing
        ${CMAKE_CURRENT_LIST_DIR}/test_routing.cpp
)

target_link_libraries(test_routing
        PRIVATE
        alpha_core
        GTest::gtest_main
)

target_compile_features(test_routing PRIVATE cxx_std_23)
alpha_strict_warnings(test_routing)

# Auto-discover TEST() cases; integrates with ctest/CLion
include(GoogleTest)
gtest_discover_tests(test_routing)


#--------------------------------  test_mem------------------------------------
add_executable(test_mem
        ${CMAKE_CURRENT_LIST_DIR}/test_mem.cpp
)
target_link_libraries(test_mem
        PRIVATE
        alpha_core
        GTest::gtest_main
)
target_compile_features(test_mem PRIVATE cxx_std_23)
alpha_strict_warnings(test_mem)
gtest_discover_tests(test_mem)



