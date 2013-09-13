//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_CANCELCALLBACKS_H_INCLUDED
#define RIPPLE_VALIDATORS_CANCELCALLBACKS_H_INCLUDED

namespace Validators
{

// Dummy CancelCallback that does nothing
//
class NoOpCancelCallback : public CancelCallback
{
public:
    bool shouldCancel ()
    {
        return false;
    }

};

//------------------------------------------------------------------------------

// CancelCallback attached to ThreadWithCallQueue
//
class ThreadCancelCallback
    : public CancelCallback
    , public Uncopyable
{
public:
    explicit ThreadCancelCallback (ThreadWithCallQueue& thread)
        : m_thread (thread)
        , m_interrupted (false)
    {
    }

    bool shouldCancel ()
    {
        if (m_interrupted)
            return true;
        return m_interrupted = m_thread.interruptionPoint ();
    }

private:
    ThreadWithCallQueue& m_thread;
    bool m_interrupted;
};

}

#endif
