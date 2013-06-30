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

class OncePerSecond::TimerSingleton
    : public SharedSingleton <OncePerSecond::TimerSingleton>
    , private InterruptibleThread::EntryPoint
{
private:
    TimerSingleton ()
        : SharedSingleton <OncePerSecond::TimerSingleton> (
            SingletonLifetime::persistAfterCreation)
        , m_thread ("Once Per Second")
    {
        m_thread.start (this);
    }

    ~TimerSingleton ()
    {
        m_thread.join ();

        bassert (m_list.empty ());
    }

    void threadRun ()
    {
        for (;;)
        {
            const bool interrupted = m_thread.wait (1000);

            if (interrupted)
                break;

            notify ();
        }
    }

    void notify ()
    {
        CriticalSection::ScopedLockType lock (m_mutex);

        for (List <Elem>::iterator iter = m_list.begin (); iter != m_list.end ();)
        {
            OncePerSecond* object = iter->object;
            ++iter;
            object->doOncePerSecond ();
        }
    }

public:
    void insert (Elem* elem)
    {
        CriticalSection::ScopedLockType lock (m_mutex);

        m_list.push_back (*elem);
    }

    void remove (Elem* elem)
    {
        CriticalSection::ScopedLockType lock (m_mutex);

        m_list.erase (m_list.iterator_to (*elem));
    }

    static TimerSingleton* createInstance ()
    {
        return new TimerSingleton;
    }

private:
    InterruptibleThread m_thread;
    CriticalSection m_mutex;
    List <Elem> m_list;
};

//------------------------------------------------------------------------------

OncePerSecond::OncePerSecond ()
{
    m_elem.instance = TimerSingleton::getInstance ();
    m_elem.object = this;
}

OncePerSecond::~OncePerSecond ()
{
}

void OncePerSecond::startOncePerSecond ()
{
    m_elem.instance->insert (&m_elem);
}

void OncePerSecond::endOncePerSecond ()
{
    m_elem.instance->remove (&m_elem);
}
