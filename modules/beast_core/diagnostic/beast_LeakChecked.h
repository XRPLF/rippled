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
        getCounter ().increment ();
    }

    LeakChecked (const LeakChecked&) noexcept
    {
        getCounter ().increment ();
    }

    ~LeakChecked ()
    {
        if (getCounter ().decrement () < 0)
        {
            /*  If you hit this, then you've managed to delete more instances
                of this class than you've created. That indicates that you're
                deleting some dangling pointers.

                Note that although this assertion will have been triggered
                during a destructor, it might not be this particular deletion
                that's at fault - the incorrect one may have happened at an
                earlier point in the program, and simply not been detected
                until now.

                Most errors like this are caused by using old-fashioned,
                non-RAII techniques for your object management. Tut, tut.
                Always, always use ScopedPointers, OwnedArrays,
                ReferenceCountedObjects, etc, and avoid the 'delete' operator
                at all costs!
            */
            DBG ("Dangling pointer deletion: " << getLeakCheckedName ());

            bassertfalse;
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

    static Counter& getCounter () noexcept
    {
        static Counter* volatile s_instance;
        static Static::Initializer s_initializer;

        if (s_initializer.beginConstruction ())
        {
            static char s_storage [sizeof (Counter)];
            s_instance = new (s_storage) Counter;

            s_initializer.endConstruction ();
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
