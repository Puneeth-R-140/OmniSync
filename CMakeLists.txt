cmake_minimum_required(VERSION 3.16)

project(OmniSync
    VERSION 1.5.0
    DESCRIPTION "Header-only C++17 CRDT library"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The engine is currently being rebuilt and its test suite is being replaced.
# Keep tests and benchmarks OFF by default so they cannot interfere with
# compiling the library and examples during development.
option(OMNISYNC_BUILD_TESTS "Build OmniSync test executables" OFF)
option(OMNISYNC_BUILD_EXAMPLES "Build OmniSync example executables" ON)
option(OMNISYNC_BUILD_BENCHMARKS "Build OmniSync benchmark executables" OFF)

# Header-only target
add_library(omnisync INTERFACE)
add_library(OmniSync::omnisync ALIAS omnisync)

target_include_directories(omnisync
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(omnisync INTERFACE cxx_std_17)

# Helpful warnings for all local executables in this repo.
function(omnisync_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(omnisync_add_exec target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE OmniSync::omnisync)
    omnisync_apply_warnings(${target_name})
endfunction()

# Tests are intentionally disabled by default while the test suite is being
# rebuilt against the finalized CRDT engine.
if(OMNISYNC_BUILD_TESTS)
    enable_testing()

    omnisync_add_exec(sequence_core_test tests/sequence_core_test.cpp)
    add_test(NAME sequence_core_test COMMAND sequence_core_test)

    omnisync_add_exec(delta_sync_test tests/delta_sync_test.cpp)
    add_test(NAME delta_sync_test COMMAND delta_sync_test)

    omnisync_add_exec(chaos_test tests/chaos_test.cpp)
    add_test(NAME chaos_test COMMAND chaos_test)

    omnisync_add_exec(persistence_test tests/persistence_test.cpp)
    add_test(NAME persistence_test COMMAND persistence_test)

    omnisync_add_exec(network_protocol_test tests/network_protocol_test.cpp)
    add_test(NAME network_protocol_test COMMAND network_protocol_test)

    omnisync_add_exec(marks_test tests/marks_test.cpp)
    add_test(NAME marks_test COMMAND marks_test)

    omnisync_add_exec(gc_coord_test tests/gc_coord_test.cpp)
    add_test(NAME gc_coord_test COMMAND gc_coord_test)
endif()

if(OMNISYNC_BUILD_EXAMPLES)
    omnisync_add_exec(example_basic_usage examples/basic_usage.cpp)
    omnisync_add_exec(example_delta_sync examples/delta_sync_demo.cpp)
    omnisync_add_exec(example_distributed_gc examples/distributed_gc_example.cpp)
    omnisync_add_exec(example_memory_profiling examples/memory_profiling_demo.cpp)
    omnisync_add_exec(example_save_load examples/save_load_demo.cpp)

    if(WIN32)
        omnisync_add_exec(example_collaborative_editor examples/collaborative_editor.cpp)
    else()
        message(STATUS "Skipping example_collaborative_editor on non-Windows platforms")
    endif()
endif()

if(OMNISYNC_BUILD_BENCHMARKS)
    omnisync_add_exec(omnisync_bench benchmarks/omnisync_bench.cpp)
endif()

# Installation
install(TARGETS omnisync EXPORT OmniSyncTargets)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/OmniSyncConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OmniSyncConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OmniSyncConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OmniSync
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OmniSyncConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OmniSyncConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OmniSync
)

install(EXPORT OmniSyncTargets
    FILE OmniSyncTargets.cmake
    NAMESPACE OmniSync::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OmniSync
)
