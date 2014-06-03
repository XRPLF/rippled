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

namespace beast
{

// Manages the list of hooks, and calls
// whoever is in the list at exit time.
//
class AtExitHook::Manager
{
public:
    Manager ()
        : m_didStaticDestruction (false)
    {
    }

    static inline Manager& get ()
    {
        return StaticObject <Manager>::get();
    }

    void insert (Item& item)
    {
        ScopedLockType lock (m_mutex);

        // Adding a new AtExitHook during or after the destruction
        // of objects with static storage duration has taken place?
        // Surely something has gone wrong.
        //
        bassert (! m_didStaticDestruction);
        m_list.push_front (item);
    }

    void erase (Item& item)
    {
        ScopedLockType lock (m_mutex);

        m_list.erase (m_list.iterator_to (item));
    }

private:
    // Called at program exit when destructors for objects
    // with static storage duration are invoked.
    //
    void doStaticDestruction ()
    {
        // In theory this shouldn't be needed (?)
        ScopedLockType lock (m_mutex);

        bassert (! m_didStaticDestruction);
        m_didStaticDestruction = true;

        for (List <Item>::iterator iter (m_list.begin()); iter != m_list.end();)
        {
            Item& item (*iter++);
            AtExitHook* const hook (item.hook ());
            hook->onExit ();
        }

        // Now do the leak checking
        //
        LeakCheckedBase::checkForLeaks ();
    }

    struct StaticDestructor
    {
        ~StaticDestructor ()
        {
            Manager::get().doStaticDestruction();
        }
    };

    typedef CriticalSection MutexType;
    typedef MutexType::ScopedLockType ScopedLockType;

    static StaticDestructor s_staticDestructor;

    MutexType m_mutex;
    List <Item> m_list;
    bool m_didStaticDestruction;
};

// This is an object with static storage duration.
// When it gets destroyed, we will call into the Manager to
// call all of the AtExitHook items in the list.
//
AtExitHook::Manager::StaticDestructor AtExitHook::Manager::s_staticDestructor;

//------------------------------------------------------------------------------

AtExitHook::Item::Item (AtExitHook* hook)
    : m_hook (hook)
{
}

AtExitHook* AtExitHook::Item::hook ()
{
    return m_hook;
}

//------------------------------------------------------------------------------

AtExitHook::AtExitHook ()
    : m_item (this)
{
#if BEAST_IOS
    // Patrick Dehne:
    //      AtExitHook::Manager::insert crashes on iOS
    //      if the storage is not accessed before it is used.
    //
    // VFALCO TODO Figure out why and fix it cleanly if needed.
    //
    char* hack = AtExitHook::Manager::s_list.s_storage;
#endif

    Manager::get().insert (m_item);
}

AtExitHook::~AtExitHook ()
{
    Manager::get().erase (m_item);
}

} // beast
