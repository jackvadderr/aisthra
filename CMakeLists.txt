cmake_minimum_required(VERSION 3.20)

project(aisthra_c17 VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

option(AISTHRA_BUILD_TESTS "Build the C17 test suite" ON)
option(AISTHRA_BUILD_EXAMPLES "Build the standalone C examples" ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

add_library(aisthra_objects OBJECT src/aisthra.c)
set_target_properties(aisthra_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(aisthra_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(aisthra_objects PUBLIC c_std_17)
target_compile_options(aisthra_objects PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
if(UNIX AND NOT APPLE)
    target_compile_definitions(aisthra_objects PRIVATE _POSIX_C_SOURCE=200809L)
endif()

add_library(aisthra STATIC $<TARGET_OBJECTS:aisthra_objects>)
add_library(aisthra_shared SHARED $<TARGET_OBJECTS:aisthra_objects>)
add_library(aisthra::static ALIAS aisthra)
add_library(aisthra::shared ALIAS aisthra_shared)

foreach(target aisthra aisthra_shared)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(${target} PUBLIC Threads::Threads)
    if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PUBLIC m)
    endif()
endforeach()

set_target_properties(aisthra_shared PROPERTIES OUTPUT_NAME aisthra)

if(AISTHRA_BUILD_TESTS)
    if(BUILD_TESTING)
        add_library(aisthra_test_objects OBJECT src/aisthra.c)
        set_target_properties(aisthra_test_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_include_directories(aisthra_test_objects PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_features(aisthra_test_objects PRIVATE c_std_17)
        target_compile_definitions(aisthra_test_objects PRIVATE AISTHRA_TESTING=1)
        if(UNIX AND NOT APPLE)
            target_compile_definitions(aisthra_test_objects PRIVATE _POSIX_C_SOURCE=200809L)
        endif()
        target_compile_options(aisthra_test_objects PRIVATE
            $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
        add_library(aisthra_test_support STATIC $<TARGET_OBJECTS:aisthra_test_objects>)
        target_include_directories(aisthra_test_support PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(aisthra_test_support PUBLIC Threads::Threads)
        if(UNIX AND NOT APPLE)
            target_link_libraries(aisthra_test_support PUBLIC m)
        endif()

        add_executable(aisthra_test_abi tests/abi/test_abi.c)
        add_executable(aisthra_test_core tests/unit/test_core.c)
        add_executable(aisthra_test_advanced tests/unit/test_advanced.c)
        add_executable(aisthra_test_concurrency tests/concurrency/test_concurrency.c)
        add_executable(aisthra_test_hardening tests/security/test_hardening.c)
        foreach(test_target
                aisthra_test_abi
                aisthra_test_core
                aisthra_test_advanced
                aisthra_test_concurrency)
            target_link_libraries(${test_target} PRIVATE aisthra)
            target_include_directories(${test_target} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include)
            target_compile_features(${test_target} PRIVATE c_std_17)
            target_compile_options(${test_target} PRIVATE
                $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
        endforeach()
        target_link_libraries(aisthra_test_hardening PRIVATE aisthra_test_support)
        target_include_directories(aisthra_test_hardening PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/support)
        target_compile_features(aisthra_test_hardening PRIVATE c_std_17)
        target_compile_options(aisthra_test_hardening PRIVATE
            $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
        add_test(NAME aisthra.abi COMMAND aisthra_test_abi)
        add_test(NAME aisthra.core COMMAND aisthra_test_core)
        add_test(NAME aisthra.advanced COMMAND aisthra_test_advanced)
        add_test(NAME aisthra.concurrency COMMAND aisthra_test_concurrency)
        add_test(NAME aisthra.hardening COMMAND aisthra_test_hardening)
        add_test(NAME aisthra.layout
            COMMAND ${CMAKE_COMMAND} -E env sh
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners/check-layout.sh)
        add_test(NAME aisthra.installed-consumer
            COMMAND ${CMAKE_COMMAND} -E env sh
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners/run-installed-consumer.sh
                ${CMAKE_BINARY_DIR})
        set_tests_properties(aisthra.core aisthra.advanced PROPERTIES LABELS "unit")
        set_tests_properties(aisthra.abi PROPERTIES LABELS "abi")
        set_tests_properties(aisthra.concurrency PROPERTIES LABELS "concurrency")
        set_tests_properties(aisthra.hardening PROPERTIES LABELS "security;memory")
        set_tests_properties(aisthra.layout PROPERTIES LABELS "integration")
        set_tests_properties(aisthra.installed-consumer PROPERTIES LABELS "integration")

        include(CheckLanguage)
        check_language(CXX)
        if(CMAKE_CXX_COMPILER)
            enable_language(CXX)
            add_executable(aisthra_test_cpp tests/abi/test_cpp.cpp)
            target_link_libraries(aisthra_test_cpp PRIVATE aisthra)
            target_include_directories(aisthra_test_cpp PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include)
            target_compile_options(aisthra_test_cpp PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-pedantic-errors;-Werror>)
            set_target_properties(aisthra_test_cpp PROPERTIES
                CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
            add_test(NAME aisthra.cpp-headers COMMAND aisthra_test_cpp)
            set_tests_properties(aisthra.cpp-headers PROPERTIES LABELS "abi")
        endif()
    endif()
endif()

if(AISTHRA_BUILD_EXAMPLES)
    add_executable(aisthra_basic_temperature examples/c/basic_temperature.c)
    target_link_libraries(aisthra_basic_temperature PRIVATE aisthra)
    target_include_directories(aisthra_basic_temperature PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(aisthra_basic_temperature PRIVATE c_std_17)
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS aisthra aisthra_shared
    EXPORT aisthraTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(EXPORT aisthraTargets
    NAMESPACE aisthra::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aisthra)
configure_package_config_file(
    cmake/aisthraConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/aisthraConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aisthra)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/aisthraConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/aisthraConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/aisthraConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aisthra)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/aisthra)
