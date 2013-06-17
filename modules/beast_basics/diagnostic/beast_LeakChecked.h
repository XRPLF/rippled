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

#ifndef BEAST_LEAKCHECKED_BEASTHEADER
#define BEAST_LEAKCHECKED_BEASTHEADER

#include "beast_Error.h"
#include "beast_Throw.h"
#include "../memory/beast_StaticObject.h"
#include "../containers/beast_LockFreeStack.h"

//
// Derived classes are automatically leak-checked on exit
//

#if BEAST_USE_LEAKCHECKED

class LeakCheckedBase
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

class LeakCheckedBase
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
