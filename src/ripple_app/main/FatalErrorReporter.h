//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_FATALERRORREPORTER_H_INCLUDED
#define RIPPLE_FATALERRORREPORTER_H_INCLUDED

/** FatalError reporter.

    This writes the details to standard error and the log. The reporter is
    installed for the lifetime of the object so typically you would put this
    at the top of main().

    An alternative is to make it a global variable but for this to cover all
    possible cases, there can be no other global variables with non trivial
    constructors that can report a fatal error. Also, the Log would need
    to be guaranteed to be set up for this handler to work.
*/
class FatalErrorReporter : public FatalError::Reporter
{
public:
    FatalErrorReporter ();
    ~FatalErrorReporter ();

    void reportMessage (String& formattedMessage);

private:
    FatalError::Reporter* m_savedReporter;
};

#endif
