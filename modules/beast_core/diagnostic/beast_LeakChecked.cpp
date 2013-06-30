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

#if BEAST_USE_LEAKCHECKED

/*============================================================================*/
// Type-independent portion of Counter
class LeakCheckedBase::CounterBase::Singleton
{
public:
    void push_back (CounterBase* counter)
    {
        m_list.push_front (counter);
    }

    void detectAllLeaks ()
    {
        for (;;)
        {
            CounterBase* counter = m_list.pop_front ();

            if (!counter)
                break;

            counter->detectLeaks ();
        }
    }

    static Singleton& getInstance ()
    {
        static Singleton instance;

        return instance;
    }

private:
    LockFreeStack <CounterBase> m_list;
};

//------------------------------------------------------------------------------

LeakCheckedBase::CounterBase::CounterBase ()
{
    Singleton::getInstance ().push_back (this);
}

void LeakCheckedBase::CounterBase::detectAllLeaks ()
{
    Singleton::getInstance ().detectAllLeaks ();
}

void LeakCheckedBase::CounterBase::detectLeaks ()
{
    // If there's a runtime error from this line, it means there's
    // an order of destruction problem between different translation units!
    //
    this->checkPureVirtual ();

    int const count = m_count.get ();

    if (count > 0)
    {
        //bassertfalse;
        DBG ("[LEAK] " << count << " of " << getClassName ());
    }
}

//------------------------------------------------------------------------------

void LeakCheckedBase::detectAllLeaks ()
{
    CounterBase::detectAllLeaks ();
}

#endif
