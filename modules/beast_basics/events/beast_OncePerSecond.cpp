/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

class OncePerSecond::TimerSingleton
    : public RefCountedSingleton <OncePerSecond::TimerSingleton>
    , private InterruptibleThread::EntryPoint
{
private:
    TimerSingleton ()
        : RefCountedSingleton <OncePerSecond::TimerSingleton> (
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
