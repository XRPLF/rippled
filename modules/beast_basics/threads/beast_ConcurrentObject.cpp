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

class ConcurrentObject::Deleter : private ThreadWithCallQueue::EntryPoints
{
private:
    Deleter () : m_thread ("AsyncDeleter")
    {
        m_thread.start (this);
    }

    ~Deleter ()
    {
        m_thread.stop (true);
    }

    void performAtExit ()
    {
        //delete this;
    }

    static void doDelete (ConcurrentObject* sharedObject)
    {
        delete sharedObject;
    }

public:
    void destroy (ConcurrentObject* sharedObject)
    {
        if (m_thread.isAssociatedWithCurrentThread ())
            delete sharedObject;
        else
            m_thread.call (&Deleter::doDelete, sharedObject);
    }

    static Deleter& getInstance ()
    {
        static Deleter instance;

        return instance;
    }

private:
    ThreadWithCallQueue m_thread;
};

//------------------------------------------------------------------------------

ConcurrentObject::ConcurrentObject ()
{
}

ConcurrentObject::~ConcurrentObject ()
{
}

void ConcurrentObject::destroyConcurrentObject ()
{
    Deleter::getInstance ().destroy (this);
}
