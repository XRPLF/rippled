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
#include <boost/thread/tss.hpp>

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

//------------------------------------------------------------------------------

#else

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <time.h>
#if BEAST_BSD
 // ???
#elif BEAST_MAC || BEAST_IOS
#include <Foundation/NSThread.h>
#include <Foundation/NSString.h>
#import <objc/message.h>
namespace beast{
#include <ripple/beast/core/osx_ObjCHelpers.h>
}

#else
#include <sys/prctl.h>

#endif

namespace beast {
namespace detail {

void setCurrentThreadNameImpl (std::string const& name)
{
   #if BEAST_IOS || (BEAST_MAC && defined (MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    BEAST_AUTORELEASEPOOL
    {
        [[NSThread currentThread] setName: stringToNS (name)];
    }
   #elif BEAST_LINUX
    #if (__GLIBC__ * 1000 + __GLIBC_MINOR__) >= 2012
     pthread_setname_np (pthread_self(), name.c_str ());
    #else
     prctl (PR_SET_NAME, name.c_str (), 0, 0, 0);
    #endif
   #endif
}

} // detail
} // beast

#endif

namespace beast {

void setCurrentThreadName (std::string name)
{
    detail::setCurrentThreadNameImpl (name);
    detail::saveThreadName (std::move (name));
}

} // beast
