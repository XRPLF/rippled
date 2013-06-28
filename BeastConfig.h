
#ifndef BEAST_BEASTCONFIG_HEADER
#define BEAST_BEASTCONFIG_HEADER

// beast_core flags:

#ifndef    BEAST_FORCE_DEBUG
 //#define BEAST_FORCE_DEBUG
#endif

#ifndef    BEAST_LOG_ASSERTIONS
 //#define BEAST_LOG_ASSERTIONS 1
#endif

#ifndef BEAST_CHECK_MEMORY_LEAKS
#define BEAST_CHECK_MEMORY_LEAKS 1
#endif

#ifndef    BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
 //#define BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
#endif

// beast_basics flags

#define BEAST_USE_BOOST 1

// We bind functions that take references, which is
// unsupported on some platforms
//
// VFALCO TODO Rewrite functions to use pointers instead
//             of references so we can get off boost::bind
//
//#define BEAST_BIND_USES_BOOST 1

#ifndef BEAST_USE_LEAKCHECKED
#define BEAST_USE_LEAKCHECKED BEAST_CHECK_MEMORY_LEAKS
#endif

#endif
