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

Static::Storage <Atomic <FatalError::Reporter*>, FatalError> FatalError::s_reporter;

void FatalError::setReporter (Reporter& reporter)
{
    s_reporter->compareAndSetBool (&reporter, nullptr);
}

void FatalError::resetReporter (Reporter& reporter)
{
    s_reporter->compareAndSetBool (nullptr, &reporter);
}

FatalError::FatalError (char const* message, char const* fileName, int lineNumber)
{
    typedef CriticalSection LockType;

    static LockType s_mutex;

    LockType::ScopedLockType lock (s_mutex);

    String const backtraceString = SystemStats::getStackBacktrace ();

    char const* const szStackBacktrace = backtraceString.toRawUTF8 ();

    String const fileNameString = File (fileName).getFileName ();

    char const* const szFileName = fileNameString.toRawUTF8 ();

    Reporter* const reporter = s_reporter->get ();

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

    class TestReporter
        : public FatalError::Reporter
        , public Uncopyable
    {
    public:
        explicit TestReporter (UnitTest& test)
            : m_test (test)
        {
        }

        void onFatalError (char const* message,
                           char const* stackBacktrace,
                           char const* fileName,
                           int lineNumber)
        {
            String s;
                
            s << "Message = '" << message << "'" << newLine;
            s << "File = '" << fileName << "' Line " << String (lineNumber) << newLine;
            s << "Stack Trace:" << newLine;
            s << stackBacktrace;

            m_test.logMessage (s);
        }

    private:
        UnitTest& m_test;
    };

    void runTest ()
    {
        beginTestCase ("raise");

        TestReporter reporter (*this);

        FatalError::setReporter (reporter);

        // We don't really expect the program to run after this
        // but the unit test is here so you can manually test it.

        FatalError ("unit test", __FILE__, __LINE__);
    }
};

static FatalErrorTests fatalErrorTests;
