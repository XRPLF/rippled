
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

// VFALCO TODO Fix this problem with FreeBSD and std::bind.
//             We need to enforce a minimum library/g++ version.
//
#if __FreeBSD__
#define BEAST_BIND_USES_BOOST 1
#endif

#ifndef BEAST_USE_LEAKCHECKED
#define BEAST_USE_LEAKCHECKED BEAST_CHECK_MEMORY_LEAKS
#endif

#endif
