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

#include <beast/utility/LeakChecked.h>
#include <beast/module/core/logging/Logger.h>

namespace beast {

namespace detail
{

class LeakCheckedBase::LeakCounterBase::Singleton
{
public:
    void push_back (LeakCounterBase* counter)
    {
        m_list.push_front (counter);
    }

    void checkForLeaks ()
    {
        for (;;)
        {
            LeakCounterBase* const counter = m_list.pop_front ();

            if (!counter)
                break;

            counter->checkForLeaks ();
        }
    }

    static Singleton& getInstance ()
    {
        static Singleton instance;

        return instance;
    }

private:
    friend class LeakCheckedBase;

    LockFreeStack <LeakCounterBase> m_list;
};

//------------------------------------------------------------------------------

LeakCheckedBase::LeakCounterBase::LeakCounterBase ()
{
    Singleton::getInstance ().push_back (this);
}

void LeakCheckedBase::LeakCounterBase::checkForLeaks ()
{
    // If there's a runtime error from this line, it means there's
    // an order of destruction problem between different translation units!
    //
    this->checkPureVirtual ();

    int const count = m_count.get ();

    if (count > 0)
    {
        /** If you hit this, then you've leaked one or more objects of the
            specified class; the name should have been printed by the line
            below.

            If you're leaking, it's probably because you're using old-fashioned,
            non-RAII techniques for your object management. Tut, tut. Always,
            always use ScopedPointers, OwnedArrays, SharedObjects,
            etc, and avoid the 'delete' operator at all costs!
        */
        BDBG ("Leaked objects: " << count << " of " << getClassName ());

        //bassertfalse;
    }
}

//------------------------------------------------------------------------------

#if BEAST_DEBUG
void LeakCheckedBase::reportDanglingPointer (char const* objectName)
#else
void LeakCheckedBase::reportDanglingPointer (char const*)
#endif
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
        SharedObjects, etc, and avoid the 'delete' operator
        at all costs!
    */
    BDBG ("Dangling pointer deletion: " << objectName);

    bassertfalse;
}

//------------------------------------------------------------------------------

void LeakCheckedBase::checkForLeaks ()
{
    LeakCounterBase::Singleton::getInstance ().checkForLeaks ();
}

}

}
