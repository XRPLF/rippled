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

#ifndef BEAST_REFERENCECOUNTEDSINGLETON_H_INCLUDED
#define BEAST_REFERENCECOUNTEDSINGLETON_H_INCLUDED

/** Thread-safe singleton which comes into existence on first use. Use this
    instead of creating objects with static storage duration. These singletons
    are automatically reference counted, so if you hold a pointer to it in every
    object that depends on it, the order of destruction of objects is assured
    to be correct.

    class Object must provide the function `Object* Object::createInstance()`

    @class SharedSingleton
    @ingroup beast_core
*/
/** @{ */
class BEAST_API SingletonLifetime
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

        /** Like createOnDemand, but after the Singleton is destroyed an
            exception will be thrown if an attempt is made to create it again.
        */
        createOnDemandOnce,

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

template <class Object>
class SharedSingleton
    : public SingletonLifetime
    , private PerformedAtExit
{
protected:
    typedef SpinLock LockType;

    /** Create the singleton.

        @param lifetime The lifetime management option.
    */
    explicit SharedSingleton (Lifetime const lifetime)
        : m_lifetime (lifetime)
    {
        bassert (s_instance == nullptr);

        if (m_lifetime == persistAfterCreation ||
            m_lifetime == neverDestroyed)
        {
            incReferenceCount ();
        }
        else if (m_lifetime == createOnDemandOnce && *s_created)
        {
            Throw (Error ().fail (__FILE__, __LINE__));
        }

        *s_created = true;
    }

    virtual ~SharedSingleton ()
    {
        bassert (s_instance == nullptr);
    }

public:
    typedef SharedPtr <Object> Ptr;

    /** Retrieve a reference to the singleton.
    */
    static Ptr getInstance ()
    {
        Ptr instance;

        instance = s_instance;

        if (instance == nullptr)
        {
            LockType::ScopedLockType lock (*s_mutex);

            instance = s_instance;

            if (instance == nullptr)
            {
                s_instance = Object::createInstance ();

                instance = s_instance;
            }
        }

        return instance;
    }

    inline void incReferenceCount () noexcept
    {
        ++m_refCount;
    }

    inline void decReferenceCount () noexcept
    {
        if (--m_refCount == 0)
            destroySingleton ();
    }

    // Caller must synchronize.
    inline bool isBeingReferenced () const
    {
        return m_refCount.get () != 0;
    }

private:
    void performAtExit ()
    {
        if (m_lifetime == SingletonLifetime::persistAfterCreation)
            decReferenceCount ();
    }

    void destroySingleton ()
    {
        bool destroy;

        // Handle the condition where one thread is releasing the last
        // reference just as another thread is trying to acquire it.
        //
        {
            LockType::ScopedLockType lock (*s_mutex);

            if (isBeingReferenced ())
            {
                destroy = false;
            }
            else
            {
                destroy = true;
                s_instance = 0;
            }
        }

        if (destroy)
        {
            bassert (m_lifetime != neverDestroyed);

            delete static_cast <Object*> (this);
        }
    }

private:
    Lifetime const m_lifetime;
    Atomic <int> m_refCount;

private:
    static Object* s_instance;
    static Static::Storage <LockType, SharedSingleton <Object> > s_mutex;
    static Static::Storage <bool, SharedSingleton <Object> > s_created;
};
/** @{ */

template <class Object>
Object* SharedSingleton <Object>::s_instance;

template <class Object>
Static::Storage <typename SharedSingleton <Object>::LockType, SharedSingleton <Object> >
SharedSingleton <Object>::s_mutex;

template <class Object>
Static::Storage <bool, SharedSingleton <Object> >
SharedSingleton <Object>::s_created;

#endif
