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

#ifndef BEAST_SHAREDSINGLETON_H_INCLUDED
#define BEAST_SHAREDSINGLETON_H_INCLUDED

#include <beast/threads/SpinLock.h>
#include <beast/smart_ptr/SharedPtr.h>
#include <beast/module/core/time/AtExitHook.h>

namespace beast
{

/** Thread-safe singleton which comes into existence on first use. Use this
    instead of creating objects with static storage duration. These singletons
    are automatically reference counted, so if you hold a pointer to it in every
    object that depends on it, the order of destruction of objects is assured
    to be correct.

    Object Requirements:
        DefaultConstructible
        TriviallyDestructible (when lifetime == neverDestroyed)
        Destructible

    @class SharedSingleton
    @ingroup beast_core
*/
/** @{ */
class SingletonLifetime
{
public:
    // It would be nice if we didn't have to qualify the enumeration but
    // Argument Dependent Lookup is inapplicable here because:
    //
    // "Base classes dependent on a template parameter aren't part of lookup."
    //  - ville
    //

    /** Construction options for SharedSingleton

        @ingroup beast_core
    */
    enum Lifetime
    {
        /** Created on first use, destroyed when the last reference is removed.
        */
        createOnDemand,

        /** The singleton is created on first use and persists until program exit.
        */
        persistAfterCreation,

        /** The singleton is created when needed and never destroyed.

            This is useful for applications which do not have a clean exit.
        */
        neverDestroyed
    };
};

//------------------------------------------------------------------------------

/** Wraps object to produce a reference counted singleton. */
template <class Object>
class SharedSingleton
    : public Object
    , private SharedObject
{
public:
    typedef SharedPtr <SharedSingleton <Object> > Ptr;

    static Ptr get (SingletonLifetime::Lifetime lifetime
        = SingletonLifetime::persistAfterCreation)
    {
        StaticData& staticData (getStaticData ());
        SharedSingleton* instance = staticData.instance;
        if (instance == nullptr)
        {
            std::lock_guard <LockType> lock (staticData.mutex);
            instance = staticData.instance;
            if (instance == nullptr)
            {
                bassert (lifetime == SingletonLifetime::createOnDemand || ! staticData.destructorCalled);
                staticData.instance = &staticData.object;
                new (staticData.instance) SharedSingleton (lifetime);
                memoryBarrier();
                instance = staticData.instance;
            }
        }
        return instance;
    }

    // DEPRECATED LEGACY FUNCTION NAME
    static Ptr getInstance (SingletonLifetime::Lifetime lifetime
        = SingletonLifetime::persistAfterCreation)
    {
        return get (lifetime);
    }

private:
    explicit SharedSingleton (SingletonLifetime::Lifetime lifetime)
        : m_lifetime (lifetime)
        , m_exitHook (this)
    {
        if (m_lifetime == SingletonLifetime::persistAfterCreation ||
            m_lifetime == SingletonLifetime::neverDestroyed)
            this->incReferenceCount ();
    }

    ~SharedSingleton ()
    {
    }

    void onExit ()
    {
        if (m_lifetime == SingletonLifetime::persistAfterCreation)
            this->decReferenceCount ();
    }

    void destroy () const
    {
        bool callDestructor;

        // Handle the condition where one thread is releasing the last
        // reference just as another thread is trying to acquire it.
        //
        {
            StaticData& staticData (getStaticData ());
            std::lock_guard <LockType> lock (staticData.mutex);

            if (this->getReferenceCount() != 0)
            {
                callDestructor = false;
            }
            else
            {
                callDestructor = true;
                staticData.instance = nullptr;
                staticData.destructorCalled = true;
            }
        }

        if (callDestructor)
        {
            bassert (m_lifetime != SingletonLifetime::neverDestroyed);

            this->~SharedSingleton();
        }
    }

    typedef SpinLock LockType;

    // This structure gets zero-filled at static initialization time.
    // No constructors are called.
    //
    class StaticData : public Uncopyable
    {
    public:
        LockType mutex;
        SharedSingleton* instance;
        SharedSingleton  object;
        bool destructorCalled;

    private:
        StaticData();
        ~StaticData();
    };

    static StaticData& getStaticData ()
    {
        static std::uint8_t storage [sizeof (StaticData)];
        return *(reinterpret_cast <StaticData*> (&storage [0]));
    }

    friend class SharedPtr <SharedSingleton>;
    friend class AtExitMemberHook <SharedSingleton>;

    SingletonLifetime::Lifetime m_lifetime;
    AtExitMemberHook <SharedSingleton> m_exitHook;
};

//------------------------------------------------------------------------------

} // beast

#endif
