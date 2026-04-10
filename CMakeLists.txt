cmake_minimum_required(VERSION 3.16)

project(OmniSync
	VERSION 1.4.0
	DESCRIPTION "Header-only C++17 CRDT library"
	LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(OMNISYNC_BUILD_TESTS "Build OmniSync test executables" ON)
option(OMNISYNC_BUILD_EXAMPLES "Build OmniSync example executables" OFF)

# Header-only target
add_library(omnisync INTERFACE)
add_library(OmniSync::omnisync ALIAS omnisync)

target_include_directories(omnisync
	INTERFACE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

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

if(OMNISYNC_BUILD_TESTS)
	enable_testing()

	omnisync_add_exec(delta_test tests/delta_sync_test.cpp)
	add_test(NAME delta_test COMMAND delta_test)

	omnisync_add_exec(fuzz_test tests/fuzz_test.cpp)
	add_test(NAME fuzz_test COMMAND fuzz_test)

	omnisync_add_exec(gc_test tests/gc_test.cpp)
	add_test(NAME gc_test COMMAND gc_test)

	omnisync_add_exec(gc_coord_test tests/gc_coord_test.cpp)
	add_test(NAME gc_coord_test COMMAND gc_coord_test)

	omnisync_add_exec(profiling_test tests/profiling_test.cpp)
	add_test(NAME profiling_test COMMAND profiling_test)

	omnisync_add_exec(save_load_test tests/save_load_test.cpp)
	add_test(NAME save_load_test COMMAND save_load_test)

	omnisync_add_exec(stability_test tests/stability_test.cpp)
	add_test(NAME stability_test COMMAND stability_test --duration-hours 0)

	omnisync_add_exec(vle_test tests/vle_compression_test.cpp)
	add_test(NAME vle_test COMMAND vle_test)
endif()

if(OMNISYNC_BUILD_EXAMPLES)
	omnisync_add_exec(example_basic_usage examples/basic_usage.cpp)
	omnisync_add_exec(example_delta_sync examples/delta_sync_demo.cpp)
	omnisync_add_exec(example_distributed_gc examples/distributed_gc_example.cpp)
	omnisync_add_exec(example_memory_profiling examples/memory_profiling_demo.cpp)
	omnisync_add_exec(example_save_load examples/save_load_demo.cpp)
endif()

install(TARGETS omnisync EXPORT OmniSyncTargets)

install(DIRECTORY include/
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/OmniSyncConfigVersion.cmake"
	VERSION ${PROJECT_VERSION}
	COMPATIBILITY SameMajorVersion)

configure_package_config_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/OmniSyncConfig.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/OmniSyncConfig.cmake"
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OmniSync)

install(FILES
	"${CMAKE_CURRENT_BINARY_DIR}/OmniSyncConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/OmniSyncConfigVersion.cmake"
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OmniSync)

install(EXPORT OmniSyncTargets
	FILE OmniSyncTargets.cmake
	NAMESPACE OmniSync::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OmniSync)
