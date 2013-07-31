//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

FatalErrorReporter::FatalErrorReporter ()
{
    FatalError::setReporter (*this);
}

FatalErrorReporter::~FatalErrorReporter ()
{
    FatalError::resetReporter (*this);
}

void FatalErrorReporter::onFatalError (
    char const* message,
    char const* stackBacktrace,
    char const* fileName,
    int lineNumber)
{
    String s;
                
    s << "Message = '" << message << "'" << newLine;
    s << "File = '" << fileName << "' Line " << String (lineNumber) << newLine;
    s << "Stack Trace:" << newLine;
    s << stackBacktrace;

    Log::out() << s;
}

//------------------------------------------------------------------------------

class FatalErrorReporterTests : public UnitTest
{
public:
    FatalErrorReporterTests () : UnitTest ("FatalErrorReporter", "ripple", runManual)
    {
    }

    void runTest ()
    {
        beginTestCase ("report");

        FatalErrorReporter reporter;

        // We don't really expect the program to run after this
        // but the unit test is here so you can manually test it.

        FatalError ("unit test", __FILE__, __LINE__);
    }
};

static FatalErrorReporterTests fatalErrorReporterTests;
