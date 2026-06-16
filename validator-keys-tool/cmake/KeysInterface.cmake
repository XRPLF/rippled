#[===================================================================[
   rippled compile options/settings via an interface library
#]===================================================================]

# cspell: words Wsuggest fprofile ftest

add_library(keys_opts INTERFACE)
add_library(Keys::opts ALIAS keys_opts)
target_compile_definitions(
    keys_opts
    INTERFACE
        BOOST_ASIO_DISABLE_HANDLER_TYPE_REQUIREMENTS
        $<$<BOOL:${boost_show_deprecated}>:
        BOOST_ASIO_NO_DEPRECATED
        BOOST_FILESYSTEM_NO_DEPRECATED
        >
        $<$<NOT:$<BOOL:${boost_show_deprecated}>>:
        BOOST_COROUTINES_NO_DEPRECATION_WARNING
        BOOST_BEAST_ALLOW_DEPRECATED
        BOOST_FILESYSTEM_DEPRECATED
        >
        $<$<BOOL:${beast_hashers}>:
        USE_BEAST_HASHER
        >
        $<$<BOOL:${beast_no_unit_test_inline}>:BEAST_NO_UNIT_TEST_INLINE=1>
        $<$<BOOL:${beast_disable_autolink}>:BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES=1>
        $<$<BOOL:${single_io_service_thread}>:RIPPLE_SINGLE_IO_SERVICE_THREAD=1>
)
target_compile_options(
    keys_opts
    INTERFACE
        $<$<AND:$<BOOL:${is_gcc}>,$<COMPILE_LANGUAGE:CXX>>:-Wsuggest-override>
        $<$<BOOL:${perf}>:-fno-omit-frame-pointer>
        $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${coverage}>>:-fprofile-arcs
        -ftest-coverage>
        $<$<AND:$<BOOL:${is_clang}>,$<BOOL:${coverage}>>:-fprofile-instr-generate
        -fcoverage-mapping>
        $<$<BOOL:${profile}>:-pg>
        $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${profile}>>:-p>
)

target_link_libraries(
    keys_opts
    INTERFACE
        $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${coverage}>>:-fprofile-arcs
        -ftest-coverage>
        $<$<AND:$<BOOL:${is_clang}>,$<BOOL:${coverage}>>:-fprofile-instr-generate
        -fcoverage-mapping>
        $<$<BOOL:${profile}>:-pg>
        $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${profile}>>:-p>
)

if(jemalloc)
    if(static)
        set(JEMALLOC_USE_STATIC ON CACHE BOOL "" FORCE)
    endif()
    find_package(jemalloc REQUIRED)
    target_compile_definitions(keys_opts INTERFACE PROFILE_JEMALLOC)
    target_include_directories(
        keys_opts
        SYSTEM
        INTERFACE ${JEMALLOC_INCLUDE_DIRS}
    )
    target_link_libraries(keys_opts INTERFACE ${JEMALLOC_LIBRARIES})
    get_filename_component(JEMALLOC_LIB_PATH ${JEMALLOC_LIBRARIES} DIRECTORY)
    ## TODO see if we can use the BUILD_RPATH target property (is it transitive?)
    set(CMAKE_BUILD_RPATH ${CMAKE_BUILD_RPATH} ${JEMALLOC_LIB_PATH})
endif()
if(san)
    target_compile_options(
        keys_opts
        INTERFACE
            # sanitizers recommend minimum of -O1 for reasonable performance
            $<$<CONFIG:Debug>:-O1>
            ${SAN_FLAG}
            -fno-omit-frame-pointer
    )
    target_compile_definitions(
        keys_opts
        INTERFACE
            $<$<STREQUAL:${san},address>:SANITIZER=ASAN>
            $<$<STREQUAL:${san},thread>:SANITIZER=TSAN>
            $<$<STREQUAL:${san},memory>:SANITIZER=MSAN>
            $<$<STREQUAL:${san},undefined>:SANITIZER=UBSAN>
    )
    target_link_libraries(keys_opts INTERFACE ${SAN_FLAG} ${SAN_LIB})
endif()
