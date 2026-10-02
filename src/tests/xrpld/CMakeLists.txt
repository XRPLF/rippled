include(GoogleTest)
include(isolate_headers)

find_package(GTest REQUIRED)

# kept separate from xrpl_tests so that the libxrpl test binary stays free of server code.
add_executable(xrpld_tests main.cpp)
patch_nix_binary(xrpld_tests)
set_target_properties(
    xrpld_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(
    xrpld_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/src"
)
# Lets tests include the shared helpers of xrpl_tests as <helpers/...>.
# Only that directory is exposed, not the rest of src/tests/libxrpl. The
# helpers these tests use are header-only, so no helper source is built in.
isolate_headers(
    xrpld_tests
    "${CMAKE_SOURCE_DIR}/src/tests/libxrpl"
    "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/helpers"
    PRIVATE
)
target_link_libraries(
    xrpld_tests
    PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl rpcspec::rpcspec
)
# The telemetry tests read ended spans back from an InMemorySpanExporter. The
# OpenTelemetry package does not put that archive on the link line, so find
# it by path, as src/tests/libxrpl/CMakeLists.txt does and explains. The
# result is cached, so this finds the same file that xrpl_tests links.
# This block follows the main link call so that the archives it adds come
# after libxrpl, which uses OpenTelemetry.
# GNU ld reads each archive once, and the umbrella lists the metrics archive,
# which these tests use, after the common and resources archives it needs.
# So on Linux it also goes before the umbrella. Apple ld and MSVC search all
# archives, and Apple ld, with fatal warnings here, rejects one listed twice.
if(telemetry)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" otel_build_type)
    find_library(
        OTEL_IN_MEMORY_EXPORTER_LIB
        NAMES opentelemetry_exporter_in_memory
        HINTS "${opentelemetry-cpp_LIB_DIRS_${otel_build_type}}"
    )
    if(NOT OTEL_IN_MEMORY_EXPORTER_LIB)
        message(FATAL_ERROR "opentelemetry_exporter_in_memory not found")
    endif()
    set(otel_libs_before_umbrella "")
    if(is_linux)
        find_library(
            OTEL_METRICS_SDK_LIB
            NAMES opentelemetry_metrics
            HINTS "${opentelemetry-cpp_LIB_DIRS_${otel_build_type}}"
        )
        if(NOT OTEL_METRICS_SDK_LIB)
            message(FATAL_ERROR "opentelemetry_metrics not found")
        endif()
        set(otel_libs_before_umbrella "${OTEL_METRICS_SDK_LIB}")
    endif()
    target_link_libraries(
        xrpld_tests
        PRIVATE
            "${OTEL_IN_MEMORY_EXPORTER_LIB}"
            ${otel_libs_before_umbrella}
            opentelemetry-cpp::opentelemetry-cpp
    )
endif()

set(test_modules rpc ledger telemetry)

foreach(module IN LISTS test_modules)
    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}.cpp"
    )
    target_sources(xrpld_tests PRIVATE ${sources})
endforeach()

gtest_discover_tests(xrpld_tests DISCOVERY_TIMEOUT 60)
