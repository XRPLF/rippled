//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/beast/core/CurrentThreadName.h>
#include <boost/predef.h>

//------------------------------------------------------------------------------

#if BOOST_OS_WINDOWS
#include <process.h>
#include <windows.h>

namespace beast::detail {

inline void
setCurrentThreadNameImpl(std::string_view name)
{
#if DEBUG && BOOST_COMP_MSVC
    // This technique is documented by Microsoft and works for all versions
    // of Windows and Visual Studio provided that the process is being run
    // under the Visual Studio debugger. For more details, see:
    // https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code

#pragma pack(push, 8)
    struct THREADNAME_INFO
    {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
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
        RaiseException(
            0x406d1388, 0, sizeof(ni) / sizeof(ULONG_PTR), (ULONG_PTR*)&ni);
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

inline void
setCurrentThreadNameImpl(std::string_view name)
{
    pthread_setname_np(name.data());
}

}  // namespace beast::detail
#endif  // BOOST_OS_MACOS

#if BOOST_OS_LINUX
#include <pthread.h>

namespace beast::detail {

inline void
setCurrentThreadNameImpl(std::string_view name)
{
    pthread_setname_np(pthread_self(), name.data());
}

}  // namespace beast::detail
#endif  // BOOST_OS_LINUX

namespace beast {

namespace detail {
thread_local std::string threadName;
}  // namespace detail

std::string
getCurrentThreadName()
{
    return detail::threadName;
}

void
setCurrentThreadName(std::string_view name)
{
    detail::threadName = name;
    detail::setCurrentThreadNameImpl(name);
}

}  // namespace beast
