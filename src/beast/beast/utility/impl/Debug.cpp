//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/utility/Debug.h>
#include <beast/unit_test/suite.h>
#include <beast/module/core/system/SystemStats.h>

namespace beast {

namespace Debug {

void breakPoint ()
{
#if BEAST_DEBUG
    if (beast_isRunningUnderDebugger ())
        beast_breakDebugger;

#else
    bassertfalse;

#endif
}

//------------------------------------------------------------------------------

#if BEAST_MSVC && defined (_DEBUG)

#if BEAST_CHECK_MEMORY_LEAKS
struct DebugFlagsInitialiser
{
    DebugFlagsInitialiser()
    {
        // Activate leak checks on exit in the MSVC Debug CRT (C Runtime)
        //
        _CrtSetDbgFlag (_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    }
};

static DebugFlagsInitialiser debugFlagsInitialiser;
#endif

void setAlwaysCheckHeap (bool bAlwaysCheck)
{
    int flags = _CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);

    if (bAlwaysCheck) flags |= _CRTDBG_CHECK_ALWAYS_DF; // on
    else flags &= ~_CRTDBG_CHECK_ALWAYS_DF; // off

    _CrtSetDbgFlag (flags);
}

void setHeapDelayedFree (bool bDelayedFree)
{
    int flags = _CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);

    if (bDelayedFree) flags |= _CRTDBG_DELAY_FREE_MEM_DF; // on
    else flags &= ~_CRTDBG_DELAY_FREE_MEM_DF; // off

    _CrtSetDbgFlag (flags);
}

void setHeapReportLeaks (bool bReportLeaks)
{
    int flags = _CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);

    if (bReportLeaks) flags |= _CRTDBG_LEAK_CHECK_DF; // on
    else flags &= ~_CRTDBG_LEAK_CHECK_DF; // off

    _CrtSetDbgFlag (flags);
}

void reportLeaks ()
{
    _CrtDumpMemoryLeaks ();
}

void checkHeap ()
{
    _CrtCheckMemory ();
}

//------------------------------------------------------------------------------

#else

void setAlwaysCheckHeap (bool)
{
}

void setHeapDelayedFree (bool)
{
}

void setHeapReportLeaks (bool)
{
}

void reportLeaks ()
{
}

void checkHeap ()
{
}

#endif

//------------------------------------------------------------------------------

String getSourceLocation (char const* fileName, int lineNumber,
                          int numberOfParents)
{
    return getFileNameFromPath (fileName, numberOfParents) +
        "(" + String::fromNumber (lineNumber) + ")";
}

String getFileNameFromPath (const char* sourceFileName, int numberOfParents)
{
    String fullPath (sourceFileName);

#if BEAST_WINDOWS
    // Convert everything to forward slash
    fullPath = fullPath.replaceCharacter ('\\', '/');
#endif

    String path;

    int chopPoint = fullPath.lastIndexOfChar ('/');
    path = fullPath.substring (chopPoint + 1);

    while (chopPoint >= 0 && numberOfParents > 0)
    {
        --numberOfParents;
        fullPath = fullPath.substring (0, chopPoint);
        chopPoint = fullPath.lastIndexOfChar ('/');
        path = fullPath.substring (chopPoint + 1) + '/' + path;
    }

    return path;
}

}

//------------------------------------------------------------------------------

// A simple unit test to determine the diagnostic settings in a build.
//
class Debug_test : public unit_test::suite
{
public:
    static int envDebug ()
    {
    #ifdef _DEBUG
        return 1;
    #else
        return 0;
    #endif
    }

    static int beastDebug ()
    {
    #ifdef BEAST_DEBUG
        return BEAST_DEBUG;
    #else
        return 0;
    #endif
    }

    static int beastForceDebug ()
    {
    #ifdef BEAST_FORCE_DEBUG
        return BEAST_FORCE_DEBUG;
    #else
        return 0;
    #endif
    }

    void run ()
    {
        log <<
            "_DEBUG                           = " <<
            String::fromNumber (envDebug ());
        
        log <<
            "BEAST_DEBUG                      = " <<
            String::fromNumber (beastDebug ());

        log <<
            "BEAST_FORCE_DEBUG                = " <<
            String::fromNumber (beastForceDebug ());

        log <<
            "sizeof(std::size_t)              = " <<
            String::fromNumber (sizeof(std::size_t));

        bassertfalse;

        fail ();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Debug,utility,beast);

}
