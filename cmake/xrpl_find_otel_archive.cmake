# Find an OpenTelemetry archive, for a test binary to link by its full path.
# Set the variable named by out to that path, and cache it under that name.
#
# A test links an archive this way when the package's targets do not put it
# on the link line where the test needs it. For example, the Conan package
# declares the in-memory exporter component with no libraries, so no target
# links that archive. Link the result before the umbrella target, so that its
# references into the SDK resolve against the umbrella's archives.
#
# CMakeDeps adds the upper-cased build type to its variable names, so the
# hint's suffix is derived here, not hardcoded as _RELEASE. An empty hint alone
# is not fatal: the Conan toolchain also puts the package's lib directory on
# CMAKE_LIBRARY_PATH, which find_library searches.
#
# Because the result is cached, every caller gets the same file. A missing
# archive stops the configure step instead of failing at link time. The check
# is explicit because find_library's REQUIRED needs CMake 3.18, and this
# project allows 3.16.
#
# xrpl_find_otel_archive(out name)
function(xrpl_find_otel_archive out name)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type)
    find_library(
        ${out}
        NAMES ${name}
        HINTS "${opentelemetry-cpp_LIB_DIRS_${build_type}}"
    )
    if(NOT ${out})
        message(FATAL_ERROR "${name} not found")
    endif()
    set(${out} "${${out}}" PARENT_SCOPE)
endfunction()
