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

namespace beast
{

//
// FatalError::Reporter
//
void FatalError::Reporter::onFatalError (
    char const* message, char const* stackBacktrace, char const* filePath, int lineNumber)
{
    String formattedMessage = formatMessage (
        message, stackBacktrace, filePath, lineNumber);

    reportMessage (formattedMessage);
}

void FatalError::Reporter::reportMessage (String& formattedMessage)
{
    std::cerr << formattedMessage.toRawUTF8 ();
}

String FatalError::Reporter::formatMessage (
    char const* message, char const* stackBacktrace, char const* filePath, int lineNumber)
{
    String formattedMessage;
    formattedMessage.preallocateBytes (16 * 1024);

    formattedMessage << message;

    if (filePath != nullptr && filePath [0] != 0)
    {
        formattedMessage << ", in " << formatFilePath (filePath)
                         << " line " << String (lineNumber);
    }

    formattedMessage << newLine;

    if (stackBacktrace != nullptr && stackBacktrace [0] != 0)
    {
        formattedMessage << "Stack:" << newLine;
        formattedMessage << stackBacktrace;
    }

    return formattedMessage;
}

String FatalError::Reporter::formatFilePath (char const* filePath)
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

    LockType::ScopedLockType lock (s_mutex);

    String const backtraceString = SystemStats::getStackBacktrace ();

    char const* const szStackBacktrace = backtraceString.toRawUTF8 ();

    String const fileNameString = fileName;

    char const* const szFileName = fileNameString.toRawUTF8 ();

    Reporter* const reporter (s_reporter);

    if (reporter != nullptr)
    {
        reporter->onFatalError (message, szStackBacktrace, szFileName, lineNumber);
    }

    Process::terminate ();
}

//------------------------------------------------------------------------------

// Yes even this class can have a unit test. It's manually triggered though.
//
class FatalErrorTests : public UnitTest
{
public:
    FatalErrorTests () : UnitTest ("FatalError", "beast", runManual)
    {
    }

    void runTest ()
    {
        beginTestCase ("raise");

        // We don't really expect the program to run after this
        // but the unit test is here so you can manually test it.
        int shouldBeZero (1);
        check_invariant (shouldBeZero == 0);
    }
};

static FatalErrorTests fatalErrorTests;

}  // namespace beast
