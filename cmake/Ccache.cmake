find_program(CCACHE_PATH "ccache")
if (CCACHE_PATH)
    if (MSVC)
        # Chocolatey uses a shim executable that we cannot use directly, in
        # which case we have to find the executable it points to.
        message(STATUS "Ccache path: ${CCACHE_PATH}")
        if("${CCACHE_PATH}" MATCHES "chocolatey")
            execute_process(
                    COMMAND ${CCACHE_PATH} --shimgen-noop
                    COMMAND Select-String "path to executable:"
                    COMMAND ForEach-Object { $_ -split " " | Select -Last 1 }
                    OUTPUT_VARIABLE CCACHE_PATH)
            message(STATUS "Ccache target: ${CCACHE_PATH}")
        endif ()

        # Tell cmake to use ccache for compiling with Visual Studio.
        cmake_path(GET CCACHE_PATH FILENAME CCACHE_FILE)
        message(STATUS "Ccache file: ${CCACHE_FILE}")
        cmake_path(GET CCACHE_PATH PARENT_PATH CCACHE_DIR)
        message(STATUS "Ccache dir: ${CCACHE_DIR}")
        set(CMAKE_VS_GLOBALS
                "CLToolExe=${CCACHE_FILE}"
                "CLToolPath=${CCACHE_DIR}"
                "UseMultiToolTask=true")

        # By default Visual Studio generators will use /Zi, which is not
        # compatible with ccache, so tell it to use /Z7 instead. When running in
        # CI the /Zi option will have already been stripped out, in which case
        # the following will be a no-op, see XrplCompiler.cmake.
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    else ()
        # For Linux and macOS we can use the ccache binary directly.
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PATH}")
    endif ()
    message(STATUS "Using ccache: ${CCACHE_PATH}")
endif ()
