/** @file
 *  Cross-platform implementation of thread-naming utilities.
 *
 *  Provides `beast::setCurrentThreadName` and `beast::getCurrentThreadName`
 *  with OS-specific backends selected at compile time via Boost.Predef macros.
 *  The canonical thread name is always stored in a `thread_local` string and
 *  is never queried back from the OS, so `getCurrentThreadName` always returns
 *  exactly what was passed to `setCurrentThreadName`.
 *
 *  @see include/xrpl/beast/core/CurrentThreadName.h
 */
#include <xrpl/beast/core/CurrentThreadName.h>

#include <string>
#include <string_view>

#if BOOST_OS_WINDOWS
#include <process.h>
#include <windows.h>

namespace beast::detail {

/** Notify the Visual Studio debugger of the current thread's name (Windows).
 *
 *  Raises the well-known Microsoft debugger exception `0x406d1388` carrying a
 *  `THREADNAME_INFO` payload. The Visual Studio debugger intercepts this
 *  exception and registers the name; all other exception handlers receive
 *  `EXCEPTION_CONTINUE_EXECUTION` so the raise is invisible to the program.
 *
 *  This body compiles only when `DEBUG && BOOST_COMP_MSVC` are both true;
 *  in release builds or under non-MSVC compilers the function is a no-op.
 *
 *  @param name The thread name to register with the debugger. Must point to
 *      a null-terminated string for the lifetime of the `RaiseException` call.
 *  @note `#pragma pack(push, 8)` ensures `THREADNAME_INFO` has the exact
 *      layout the debugger expects regardless of the ambient pack setting.
 *  @see https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code
 */
inline void
setCurrentThreadNameImpl(std::string_view name)
{
#if DEBUG && BOOST_COMP_MSVC
#pragma pack(push, 8)
    /** Payload struct for the Visual Studio thread-name debugger exception. */
    struct THREADNAME_INFO
    {
        DWORD dwType;     /**< Must be 0x1000. */
        LPCSTR szName;    /**< Pointer to the null-terminated thread name. */
        DWORD dwThreadID; /**< Thread ID (-1 for the calling thread). */
        DWORD dwFlags;    /**< Reserved; must be zero. */
    };
#pragma pack(pop)

    THREADNAME_INFO ni;

    ni.dwType = 0x1000;
    ni.szName = name.data();
    ni.dwThreadID = GetCurrentThreadId();
    ni.dwFlags = 0;

#pragma warning(push)
#pragma warning(disable : 6320 6322)
    __try
    {
        RaiseException(0x406d1388, 0, sizeof(ni) / sizeof(ULONG_PTR), (ULONG_PTR*)&ni);
    }
    __except (EXCEPTION_CONTINUE_EXECUTION)
    {
    }
#pragma warning(pop)
#endif
}

}  // namespace beast::detail
#endif  // BOOST_OS_WINDOWS

#if BOOST_OS_MACOS
#include <pthread.h>

namespace beast::detail {

/** Notify the OS of the current thread's name (macOS).
 *
 *  Calls the one-argument Darwin variant of `pthread_setname_np`, which
 *  names only the calling thread (unlike the POSIX two-argument form).
 *
 *  @param name The thread name. The underlying string data must be
 *      null-terminated; callers always pass either a string literal or a
 *      `std::string`, satisfying this invariant.
 */
inline void
setCurrentThreadNameImpl(std::string_view name)
{
    pthread_setname_np(name.data());  // NOLINT(bugprone-suspicious-stringview-data-usage)
}

}  // namespace beast::detail
#endif  // BOOST_OS_MACOS

#if BOOST_OS_LINUX
#include <pthread.h>

#include <cstdio>
#include <iostream>  // IWYU pragma: keep

namespace beast::detail {

/** Notify the OS of the current thread's name (Linux).
 *
 *  Linux enforces a hard kernel limit of 16 bytes (including the null
 *  terminator) via `pthread_setname_np`; names longer than 15 characters
 *  cause `ERANGE` and the name is not set at all. To avoid this silent
 *  failure, `name` is manually truncated into a stack buffer before the
 *  `pthread_setname_np` call.
 *
 *  If the build macro `TRUNCATED_THREAD_NAME_LOGS` is defined, a warning is
 *  emitted to `std::cerr` whenever truncation occurs.
 *
 *  @param name The desired thread name. Names longer than
 *      `kMAX_THREAD_NAME_LENGTH` (15) characters are silently truncated to
 *      fit the kernel limit.
 *  @note The header's template overload of `setCurrentThreadName` catches
 *      oversized string literals at compile time; this runtime truncation
 *      handles the `std::string_view` overload where the length is unknown
 *      until runtime.
 */
inline void
setCurrentThreadNameImpl(std::string_view name)
{
    char boundedName[kMAX_THREAD_NAME_LENGTH + 1];
    auto const boundedSize =
        name.size() < kMAX_THREAD_NAME_LENGTH ? name.size() : kMAX_THREAD_NAME_LENGTH;
    name.copy(boundedName, boundedSize);
    boundedName[boundedSize] = '\0';

    pthread_setname_np(pthread_self(), boundedName);

#ifdef TRUNCATED_THREAD_NAME_LOGS
    if (name.size() > kMAX_THREAD_NAME_LENGTH)
    {
        std::cerr << "WARNING: Thread name \"" << name << "\" (length " << name.size()
                  << ") exceeds maximum of " << kMAX_THREAD_NAME_LENGTH
                  << " characters on Linux.\n";
    }
#endif
}

}  // namespace beast::detail
#endif  // BOOST_OS_LINUX

namespace beast {

namespace detail {
/** Thread-local storage for the name assigned to the current thread.
 *
 *  Written by `setCurrentThreadName` and read by `getCurrentThreadName`.
 *  Storing the name here — rather than querying the OS — guarantees that
 *  `getCurrentThreadName` always returns exactly the string that was passed
 *  in, with no platform-imposed truncation or encoding changes.
 */
thread_local std::string gThreadName;
}  // namespace detail

std::string
getCurrentThreadName()
{
    return detail::gThreadName;
}

void
setCurrentThreadName(std::string_view name)
{
    detail::gThreadName = name;
    detail::setCurrentThreadNameImpl(name);
}

}  // namespace beast
