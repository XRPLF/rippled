//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#if _MSC_VER

#include <windows.h>

namespace ripple {

// VFALCO TODO use beast::Thread::setCurrentThreadName() or something similar.

void setCallingThreadName (char const* threadName)
{
    struct ThreadInfo
    {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
    };

    ThreadInfo info;

    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = GetCurrentThreadId ();
    info.dwFlags = 0;

    __try
    {
        // This is a VisualStudio specific exception
        RaiseException (0x406d1388, 0, sizeof (info) / sizeof (ULONG_PTR), (ULONG_PTR*) &info);
    }
    __except (EXCEPTION_CONTINUE_EXECUTION)
    {
    }
}

} // ripple

#else

namespace ripple {

#ifdef PR_SET_NAME
#define HAVE_NAME_THREAD
extern void setCallingThreadName (const char* n)
{
    static std::string pName;

    if (pName.empty ())
    {
        std::ifstream cLine ("/proc/self/cmdline", std::ios::in);
        cLine >> pName;

        if (pName.empty ())
            pName = "rippled";
        else
        {
            size_t zero = pName.find_first_of ('\0');

            if ((zero != std::string::npos) && (zero != 0))
                pName = pName.substr (0, zero);

            size_t slash = pName.find_last_of ('/');

            if (slash != std::string::npos)
                pName = pName.substr (slash + 1);
        }

        pName += " ";
    }

    // VFALCO TODO Use beast::Thread::setCurrentThreadName here
    //
    prctl (PR_SET_NAME, (pName + n).c_str (), 0, 0, 0);
}
#endif

#ifndef HAVE_NAME_THREAD
extern void setCallingThreadName (const char*)
{
}
#endif

} // ripple

#endif
