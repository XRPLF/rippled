find_program(CCACHE_PATH "ccache")
if (CCACHE_PATH)
    if (MSVC)
        # Chocolatey uses a shim executable that we cannot use directly, in
        # which case we have to find the executable it points to. If we cannot
        # find the target executable then we cannot use ccache.
        message(STATUS "Ccache path: ${CCACHE_PATH}")
        if ("${CCACHE_PATH}" MATCHES "chocolatey")
            find_program(BASH_PATH "bash")
            if (NOT BASH_PATH)
                message(WARNING "Could not find bash.")
                return()
            endif ()
            execute_process(
                    COMMAND bash -c "export LC_ALL='en_US.UTF-8'; ${CCACHE_PATH} --shimgen-noop | grep -oP 'path to executable: \\K.+' | head -c -1"
                    OUTPUT_VARIABLE CCACHE_PATH)
            if (NOT CCACHE_PATH)
                message(WARNING "Could not find ccache target.")
                return()
            endif ()
            file(TO_CMAKE_PATH "${CCACHE_PATH}" CCACHE_PATH)
        endif ()

        # Tell cmake to use ccache for compiling with Visual Studio.
        file(COPY_FILE
                ${CCACHE_PATH} ${CMAKE_BINARY_DIR}/cl.exe
                ONLY_IF_DIFFERENT)
        set(CMAKE_VS_GLOBALS
                "CLToolExe=cl.exe"
                "CLToolPath=${CMAKE_BINARY_DIR}"
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
