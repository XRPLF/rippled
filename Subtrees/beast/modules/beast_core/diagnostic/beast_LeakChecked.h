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

#ifndef BEAST_LEAKCHECKED_BEASTHEADER
#define BEAST_LEAKCHECKED_BEASTHEADER

//
// Derived classes are automatically leak-checked on exit
//

#if BEAST_USE_LEAKCHECKED

class BEAST_API LeakCheckedBase
{
public:
    static void detectAllLeaks ();

protected:
    class CounterBase : public LockFreeStack <CounterBase>::Node
    {
    public:
        CounterBase ();

        virtual ~CounterBase () { }

        inline int increment ()
        {
            return ++m_count;
        }

        inline int decrement ()
        {
            return --m_count;
        }

        virtual char const* getClassName () const = 0;

        static void detectAllLeaks ();

    private:
        void detectLeaks ();

        virtual void checkPureVirtual () const = 0;

    protected:
        class Singleton;

        Atomic <int> m_count;
    };
};

//------------------------------------------------------------------------------

/** Detects leaks at program exit.

    To use this, derive your class from this template using CRTP (curiously
    recurring template pattern).
*/
template <class Object>
class LeakChecked : private LeakCheckedBase
{
protected:
    LeakChecked () noexcept
    {
        if (getLeakCheckedCounter ().increment () == 0)
        {
            DBG ("[LOGIC] " << getLeakCheckedName ());
            Throw (Error ().fail (__FILE__, __LINE__));
        }
    }

    LeakChecked (const LeakChecked&) noexcept
    {
        if (getLeakCheckedCounter ().increment () == 0)
        {
            DBG ("[LOGIC] " << getLeakCheckedName ());
            Throw (Error ().fail (__FILE__, __LINE__));
        }
    }

    ~LeakChecked ()
    {
        if (getLeakCheckedCounter ().decrement () < 0)
        {
            DBG ("[LOGIC] " << getLeakCheckedName ());
            Throw (Error ().fail (__FILE__, __LINE__));
        }
    }

private:
    class Counter : public CounterBase
    {
    public:
        Counter () noexcept
        {
        }

        char const* getClassName () const
        {
            return getLeakCheckedName ();
        }

        void checkPureVirtual () const { }
    };

private:
    /* Due to a bug in Visual Studio 10 and earlier, the string returned by
       typeid().name() will appear to leak on exit. Therefore, we should
       only call this function when there's an actual leak, or else there
       will be spurious leak notices at exit.
    */
    static const char* getLeakCheckedName ()
    {
        return typeid (Object).name ();
    }

    static Counter& getLeakCheckedCounter () noexcept
    {
        static Counter* volatile s_instance;
        static Static::Initializer s_initializer;

        if (s_initializer.begin ())
        {
            static char s_storage [sizeof (Counter)];
            s_instance = new (s_storage) Counter;
            s_initializer.end ();
        }

        return *s_instance;
    }
};

#else

class BEAST_API LeakCheckedBase
{
private:
    friend class PerformedAtExit;

    static void detectAllLeaks () { }
};

template <class Object>
struct LeakChecked : LeakCheckedBase
{
};

#endif

#endif
