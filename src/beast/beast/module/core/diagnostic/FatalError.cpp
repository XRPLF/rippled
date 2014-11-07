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

#include <beast/module/core/diagnostic/FatalError.h>
#include <beast/unit_test/suite.h>

namespace beast {

//
// FatalError::Reporter
//
void FatalError::Reporter::onFatalError (
    char const* message,
    char const* backtrace,
    char const* filePath,
    int lineNumber)
{
    reportMessage (formatMessage (message, backtrace, filePath, lineNumber));
}

void FatalError::Reporter::reportMessage (std::string const& message)
{
    std::cerr << message << std::endl;
}

std::string FatalError::Reporter::formatMessage (
    char const* message,
    char const* backtrace,
    char const* filePath,
    int lineNumber)
{
    std::string output;
    output.reserve (16 * 1024);

    output.append (message);

    if (filePath != nullptr && filePath [0] != 0)
    {
        output.append (", in ");
        output.append (formatFilePath (filePath));
        output.append (" line ");
        output.append (std::to_string (lineNumber));
    }

    output.append ("\n");

    if (backtrace != nullptr && backtrace[0] != 0)
    {
        output.append ("Stack:\n");
        output.append (backtrace);
    }

    return output;
}

std::string FatalError::Reporter::formatFilePath (char const* filePath)
{
    return filePath;
}

//------------------------------------------------------------------------------

FatalError::Reporter *FatalError::s_reporter;

/** Returns the current fatal error reporter. */
FatalError::Reporter* FatalError::getReporter ()
{
    return s_reporter;
}

FatalError::Reporter* FatalError::setReporter (Reporter* reporter)
{
    Reporter* const previous (s_reporter);
    s_reporter = reporter;
    return previous;
}

FatalError::FatalError (char const* message, char const* fileName, int lineNumber)
{
    typedef CriticalSection LockType;

    static LockType s_mutex;

    std::lock_guard <LockType> lock (s_mutex);

    auto const backtrace = SystemStats::getStackBacktrace ();

    Reporter* const reporter = s_reporter;

    if (reporter != nullptr)
    {
        reporter->onFatalError (message, backtrace.c_str (), fileName, lineNumber);
    }

    Process::terminate ();
}

//------------------------------------------------------------------------------

class FatalError_test : public unit_test::suite
{
public:
    void run ()
    {
        int shouldBeZero (1);
        check_invariant (shouldBeZero == 0);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(FatalError,beast_core,beast);

} // beast
