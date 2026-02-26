find_program(CCACHE_PATH "ccache")
if (NOT CCACHE_PATH)
    return()
endif ()

# For Linux and macOS we can use the ccache binary directly.
if (NOT MSVC)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PATH}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PATH}")
    message(STATUS "Found ccache: ${CCACHE_PATH}")
    return()
endif ()

# For Windows more effort is required. The code below is a modified version of
# https://github.com/ccache/ccache/wiki/MS-Visual-Studio#usage-with-cmake.
if ("${CCACHE_PATH}" MATCHES "chocolatey")
    message(DEBUG "Ccache path: ${CCACHE_PATH}")
    # Chocolatey uses a shim executable that we cannot use directly, in which case we have to find the executable it
    # points to. If we cannot find the target executable then we cannot use ccache.
    find_program(BASH_PATH "bash")
    if (NOT BASH_PATH)
        message(WARNING "Could not find bash.")
        return()
    endif ()

    execute_process(COMMAND bash -c
                            "export LC_ALL='en_US.UTF-8'; ${CCACHE_PATH} --shimgen-noop | grep -oP 'path to executable: \\K.+' | head -c -1"
                    OUTPUT_VARIABLE CCACHE_PATH)

    if (NOT CCACHE_PATH)
        message(WARNING "Could not find ccache target.")
        return()
    endif ()
    file(TO_CMAKE_PATH "${CCACHE_PATH}" CCACHE_PATH)
endif ()
message(STATUS "Found ccache: ${CCACHE_PATH}")

# Tell cmake to use ccache for compiling with Visual Studio.
file(COPY_FILE ${CCACHE_PATH} ${CMAKE_BINARY_DIR}/cl.exe ONLY_IF_DIFFERENT)
set(CMAKE_VS_GLOBALS "CLToolExe=cl.exe" "CLToolPath=${CMAKE_BINARY_DIR}" "TrackFileAccess=false"
                     "UseMultiToolTask=true")

# By default Visual Studio generators will use /Zi to capture debug information, which is not compatible with ccache, so
# tell it to use /Z7 instead.
if (MSVC)
    foreach (var_ CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE)
        string(REPLACE "/Zi" "/Z7" ${var_} "${${var_}}")
    endforeach ()
endif ()
