// Portions of this file are from JUCE (http://www.juce.com).
// Copyright (c) 2013 - Raw Material Software Ltd.
// Please visit http://www.juce.com

#pragma once

#include <boost/predef.h>

#include <string>
#include <string_view>

namespace beast {

/** Changes the name of the caller thread.
    Different OSes may place different length or content limits on this name.
*/
void
setCurrentThreadName(std::string_view newThreadName);

#if BOOST_OS_LINUX

// On Linux, thread names are limited to 16 bytes including the null terminator.
// Maximum number of characters is therefore 15.
constexpr std::size_t maxThreadNameLength = 15;

/** Sets the name of the caller thread with compile-time size checking.
    @tparam N The size of the string literal including null terminator
    @param newThreadName A string literal to set as the thread name

    This template overload enforces that thread names are at most 16 characters
    (including null terminator) at compile time, matching Linux's limit.
*/
template <std::size_t N>
void
setCurrentThreadName(char const (&newThreadName)[N])
{
    static_assert(N <= maxThreadNameLength + 1, "Thread name cannot exceed 15 characters");

    setCurrentThreadName(std::string_view(newThreadName, N - 1));
}
#endif

/** Returns the name of the caller thread.

    The name returned is the name as set by a call to setCurrentThreadName().
    If the thread name is set by an external force, then that name change
    will not be reported.

    If no name has ever been set, then the empty string is returned.
*/
std::string
getCurrentThreadName();

}  // namespace beast
