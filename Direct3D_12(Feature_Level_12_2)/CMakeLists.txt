cmake_minimum_required(VERSION 3.24)
project(ADX12Frontend VERSION 0.1.0 LANGUAGES C)

option(ADX12_BUILD_TESTS "Build ADX12 frontend tests" ON)

add_library(adx12_frontend STATIC
    src/frontend/frontend_environment.c)
target_include_directories(adx12_frontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(adx12_frontend PUBLIC c_std_11)
target_compile_options(adx12_frontend PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror)

add_executable(adx12-frontend-probe tools/frontend_probe.c)
target_link_libraries(adx12-frontend-probe PRIVATE adx12_frontend)

if(ADX12_BUILD_TESTS)
    enable_testing()
    add_executable(adx12-frontend-environment-test
        tests/frontend_environment_test.c)
    target_link_libraries(adx12-frontend-environment-test PRIVATE adx12_frontend)
    add_test(
        NAME adx12.frontend.environment
        COMMAND adx12-frontend-environment-test)
endif()
