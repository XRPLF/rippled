//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ValidatorsImp
    : public Validators
    , private InterruptibleThread::EntryPoint
{
public:
    explicit ValidatorsImp (Listener* listener)
        : m_thread ("Validators")
        , m_listener (listener)
    {
    }

    ~ValidatorsImp ()
    {
    }

    void addTrustedUri (String uri)
    {
        m_trustedUris.add (uri);
    }

    void start ()
    {
        m_thread.start (this);
    }

    void threadRun ()
    {
        // process the trustedUri list and blah blah
    }

private:
    InterruptibleThread m_thread;
    Listener* const m_listener;
    StringArray m_trustedUris;
};

Validators* Validators::New (Listener* listener)
{
    return new ValidatorsImp (listener);
}
