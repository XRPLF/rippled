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

#ifndef BEAST_REFERENCECOUNTEDSINGLETON_BEASTHEADER
#define BEAST_REFERENCECOUNTEDSINGLETON_BEASTHEADER

#include "../events/beast_PerformedAtExit.h"
#include "../memory/beast_StaticObject.h"

/**
  Thread-safe singleton which comes into existence on first use. Use this
  instead of creating objects with static storage duration. These singletons
  are automatically reference counted, so if you hold a pointer to it in every
  object that depends on it, the order of destruction of objects is assured
  to be correct.

  class Object must provide the function `Object* Object::createInstance()`

  @class RefCountedSingleton
  @ingroup beast_core
*/
/** @{ */
class SingletonLifetime
{
    // "base classes dependent on a template parameter
    // aren't part of lookup." - ville
public:
    /**
      Construction options for RefCountedSingleton

      @ingroup beast_core
    */
    enum Lifetime
    {
        /** Singleton is created on first use and destroyed when
            the last reference is removed.
        */
        createOnDemand,

        /** Like createOnDemand, but after the Singleton is destroyed an
            exception will be thrown if an attempt is made to create it again.
        */
        createOnDemandOnce,

        /** The singleton is created on first use and persists until program exit.
        */
        persistAfterCreation
    };
};

template <class Object>
class RefCountedSingleton
    : public SingletonLifetime
    , private PerformedAtExit
{
protected:
    typedef SpinLock LockType;

    /** Create the singleton.

        @param lifetime The lifetime management option.
    */
    explicit RefCountedSingleton (Lifetime const lifetime)
        : m_lifetime (lifetime)
    {
        bassert (s_instance == nullptr);

        if (m_lifetime == persistAfterCreation)
        {
            incReferenceCount ();
        }
        else if (m_lifetime == createOnDemandOnce && *s_created)
        {
            Throw (Error ().fail (__FILE__, __LINE__));
        }

        *s_created = true;
    }

    virtual ~RefCountedSingleton ()
    {
        bassert (s_instance == nullptr);
    }

public:
    typedef ReferenceCountedObjectPtr <Object> Ptr;

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
        m_refs.addref ();
    }

    inline void decReferenceCount () noexcept
    {
        if (m_refs.release ())
            destroySingleton ();
    }

    // Caller must synchronize.
    inline bool isBeingReferenced () const
    {
        return m_refs.isSignaled ();
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
            delete this;
        }
    }

private:
    Lifetime const m_lifetime;
    AtomicCounter m_refs;

private:
    static Object* s_instance;
    static Static::Storage <LockType, RefCountedSingleton <Object> > s_mutex;
    static Static::Storage <bool, RefCountedSingleton <Object> > s_created;
};
/** @{ */

template <class Object>
Object* RefCountedSingleton <Object>::s_instance;

template <class Object>
Static::Storage <typename RefCountedSingleton <Object>::LockType, RefCountedSingleton <Object> >
RefCountedSingleton <Object>::s_mutex;

template <class Object>
Static::Storage <bool, RefCountedSingleton <Object> >
RefCountedSingleton <Object>::s_created;

#endif
