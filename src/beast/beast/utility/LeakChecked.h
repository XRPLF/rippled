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

#ifndef BEAST_UTILITY_LEAKCHECKED_H_INCLUDED
#define BEAST_UTILITY_LEAKCHECKED_H_INCLUDED

#include <beast/Config.h>
#include <beast/Atomic.h>
#include <beast/intrusive/LockFreeStack.h>
#include <beast/utility/StaticObject.h>

namespace beast {

namespace detail {

class LeakCheckedBase
{
public:
    static void checkForLeaks ();

protected:
    class LeakCounterBase : public LockFreeStack <LeakCounterBase>::Node
    {
    public:
        LeakCounterBase ();

        virtual ~LeakCounterBase ()
        {
        }

        inline int increment ()
        {
            return ++m_count;
        }

        inline int decrement ()
        {
            return --m_count;
        }

        virtual char const* getClassName () const = 0;

    private:
        void checkForLeaks ();
        virtual void checkPureVirtual () const = 0;

        class Singleton;
        friend class LeakCheckedBase;

        Atomic <int> m_count;
    };

    static void reportDanglingPointer (char const* objectName);
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

    LeakChecked (LeakChecked const&) noexcept
    {
        getCounter ().increment ();
    }

    ~LeakChecked ()
    {
        if (getCounter ().decrement () < 0)
        {
            reportDanglingPointer (getLeakCheckedName ());
        }
    }

private:
    // Singleton that maintains the count of this object
    //
    class LeakCounter : public LeakCounterBase
    {
    public:
        LeakCounter () noexcept
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

    // Retrieve the singleton for this object
    //
    static LeakCounter& getCounter () noexcept
    {
        return StaticObject <LeakCounter>::get();
    }
};

}

//------------------------------------------------------------------------------

namespace detail
{

namespace disabled
{

class LeakCheckedBase
{
public:
    static void checkForLeaks ()
    {
    }
};

template <class Object>
class LeakChecked : public LeakCheckedBase
{
public:
};

}

}

//------------------------------------------------------------------------------

// Lift the appropriate implementation into our namespace
//
#if BEAST_CHECK_MEMORY_LEAKS
using detail::LeakChecked;
using detail::LeakCheckedBase;
#else
using detail::disabled::LeakChecked;
using detail::disabled::LeakCheckedBase;
#endif

}

#endif
