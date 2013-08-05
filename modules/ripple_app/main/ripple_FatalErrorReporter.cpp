//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifdef TWICE
#error die
#endif

#define TWICE

FatalErrorReporter::FatalErrorReporter ()
{
    FatalError::setReporter (*this);
}

FatalErrorReporter::~FatalErrorReporter ()
{
    FatalError::resetReporter (*this);
}

void FatalErrorReporter::reportMessage (String& formattedMessage)
{
    Log::out() << formattedMessage.toRawUTF8 ();
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

        FatalError ("The unit test intentionally failed", __FILE__, __LINE__);
    }
};

static FatalErrorReporterTests fatalErrorReporterTests;
