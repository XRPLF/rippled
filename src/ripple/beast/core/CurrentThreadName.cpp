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
#include <ripple/beast/core/Config.h>
#include <boost/thread/tss.hpp>
#include <ripple/beast/core/BasicNativeHeaders.h>
#include <ripple/beast/core/StandardIncludes.h>

namespace beast {
namespace detail {

static boost::thread_specific_ptr<std::string> threadName;

void saveThreadName (std::string name)
{
    threadName.reset (new std::string {std::move(name)});
}

} // detail

boost::optional<std::string> getCurrentThreadName ()
{
    if (auto r = detail::threadName.get())
        return *r;
    return boost::none;
}

} // beast

//------------------------------------------------------------------------------

#if BEAST_WINDOWS

#include <windows.h>
#include <process.h>
#include <tchar.h>

namespace beast {
namespace detail {

void setCurrentThreadNameImpl (std::string const& name)
{
   #if BEAST_DEBUG && BEAST_MSVC
    struct
    {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
    } info;

    info.dwType = 0x1000;
    info.szName = name.c_str ();
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;

    __try
    {
        RaiseException (0x406d1388 /*MS_VC_EXCEPTION*/, 0, sizeof (info) / sizeof (ULONG_PTR), (ULONG_PTR*) &info);
    }
    __except (EXCEPTION_CONTINUE_EXECUTION)
    {}
   #else
    (void) name;
   #endif
}

} // detail
} // beast

#elif BEAST_MAC

#include <pthread.h>

namespace beast {
namespace detail {

void setCurrentThreadNameImpl (std::string const& name)
{
    pthread_setname_np(name.c_str());
}

} // detail
} // beast

#else  // BEAST_LINUX

#include <pthread.h>

namespace beast {
namespace detail {

void setCurrentThreadNameImpl (std::string const& name)
{
    pthread_setname_np(pthread_self(), name.c_str());
}

} // detail
} // beast

#endif  // BEAST_LINUX

namespace beast {

void setCurrentThreadName (std::string name)
{
    detail::setCurrentThreadNameImpl (name);
    detail::saveThreadName (std::move (name));
}

} // beast
