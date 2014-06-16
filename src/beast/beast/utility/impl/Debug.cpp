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

// Returns a String with double quotes escaped
static const String withEscapedQuotes (String const& string)
{
    String escaped;

    int i0 = 0;
    int i;

    do
    {
        i = string.indexOfChar (i0, '"');

        if (i == -1)
        {
            escaped << string.substring (i0, string.length ());
        }
        else
        {
            escaped << string.substring (i0, i) << "\\\"";
            i0 = i + 1;
        }
    }
    while (i != -1);

    return escaped;
}

// Converts escaped quotes back into regular quotes
static const String withUnescapedQuotes (String const& string)
{
    String unescaped;

    int i0 = 0;
    int i;

    do
    {
        i = string.indexOfChar (i0, '\\');

        if (i == -1)
        {
            unescaped << string.substring (i0, string.length ());
        }
        else
        {
            // peek
            if (string.length () > i && string[i + 1] == '\"')
            {
                unescaped << string.substring (i0, i) << '"';
                i0 = i + 2;
            }
            else
            {
                unescaped << string.substring (i0, i + 1);
                i0 = i + 1;
            }
        }
    }
    while (i != -1);

    return unescaped;
}

// Converts a String that may contain newlines, into a
// command line where each line is delimited with quotes.
// Any quotes in the actual string will be escaped via \".
String stringToCommandLine (String const& string)
{
    String commandLine;

    int i0 = 0;
    int i;

    for (i = 0; i < string.length (); i++)
    {
        beast_wchar c = string[i];

        if (c == '\n')
        {
            if (i0 != 0)
                commandLine << ' ';

            commandLine << '"' << withEscapedQuotes (string.substring (i0, i)) << '"';
            i0 = i + 1;
        }
    }

    if (i0 < i)
    {
        if (i0 != 0)
            commandLine << ' ';

        commandLine << '"' << withEscapedQuotes (string.substring (i0, i)) << '"';
    }

    return commandLine;
}

// Converts a command line consisting of multiple quoted strings
// back into a single string with newlines delimiting each quoted
// string. Escaped quotes \" are turned into real quotes.
String commandLineToString (const String& commandLine)
{
    String string;

    bool quoting = false;
    int i0 = 0;
    int i;

    for (i = 0; i < commandLine.length (); i++)
    {
        beast_wchar c = commandLine[i];

        if (c == '\\')
        {
            // peek
            if (commandLine.length () > i && commandLine[i + 1] == '\"')
            {
                i++;
            }
        }
        else if (c == '"')
        {
            if (!quoting)
            {
                i0 = i + 1;
                quoting = true;
            }
            else
            {
                if (!string.isEmpty ())
                    string << '\n';

                string << withUnescapedQuotes (commandLine.substring (i0, i));
                quoting = false;
            }
        }
    }

    return string;
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
            "operatingSystemName              = '" <<
            SystemStats::getOperatingSystemName () << "'";
        
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
