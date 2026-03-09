include(CompilationEnv)
include(XrplSanitizers)

find_package(
    Boost
    REQUIRED
    COMPONENTS
        chrono
        container
        context
        date_time
        filesystem
        json
        program_options
        regex
        system
        thread
)

add_library(xrpl_boost INTERFACE)
add_library(Xrpl::boost ALIAS xrpl_boost)

target_link_libraries(
    xrpl_boost
    INTERFACE
        Boost::headers
        Boost::chrono
        Boost::container
        Boost::context
        Boost::date_time
        Boost::filesystem
        Boost::json
        Boost::process
        Boost::program_options
        Boost::regex
        Boost::thread
)
if(Boost_COMPILER)
    target_link_libraries(xrpl_boost INTERFACE Boost::disable_autolinking)
endif()

# Boost.Context's ucontext backend has ASAN fiber-switching annotations
# (start/finish_switch_fiber) that are compiled in when BOOST_USE_ASAN is defined.
# This tells ASAN about fiber stack switches used by boost::asio::spawn,
# preventing false positive stack-use-after-scope errors.
# BOOST_USE_UCONTEXT ensures the ucontext backend is selected (fcontext does
# not support ASAN annotations).
# These defines must match what Boost was compiled with (see conan/profiles/sanitizers).
if(enable_asan)
    target_compile_definitions(
        xrpl_boost
        INTERFACE BOOST_USE_ASAN BOOST_USE_UCONTEXT
    )
endif()
