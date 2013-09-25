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

#ifndef BEAST_STATICOBJECT_H_INCLUDED
#define BEAST_STATICOBJECT_H_INCLUDED

//
// A full suite of thread-safe objects designed for static storage duration.
//
// Wraps an object with a thread-safe initialization preamble so that it can
// properly exist with static storage duration.
//
// Implementation notes:
//
//   This is accomplished by omitting the constructor and relying on the C++
//   specification that plain data types with static storage duration are filled
//   with zeroes before any other initialization code executes.
//
// Spec: N2914=09-0104
//
// [3.6.2] Initialization of non-local objects
//
//         Objects with static storage duration (3.7.1) or thread storage
//         duration (3.7.2) shall be zero-initialized (8.5) before any
//         other initialization takes place.
//
// Requirements:
//
//  Object must be constructible without parameters.
//  The StaticObject must be declared with static storage duration or
//    the behavior is undefined.
//
// Usage example:
//
// Object* getInstance ()
// {
//   static StaticObject <Object> instance;
//   return instance->getObject ();
// }
//

namespace Static
{

//------------------------------------------------------------------------------

// Holds an object with static storage duration.
// The owner determines if and when the object is constructed and destroyed.
// Caller is responsible for synchronization.
//
template <class ObjectType, class Tag>
class Storage
{
public:
    static inline void construct ()
    {
        new (getObjectPtr ()) ObjectType;
    }

    static inline void destroy ()
    {
        getObjectPtr ()->~ObjectType ();
    }

    static inline ObjectType* getObjectPtr ()
    {
        return reinterpret_cast <ObjectType*> (s_storage);
    }

    static inline ObjectType& getObject ()
    {
        return *getObjectPtr ();
    }

    inline ObjectType* operator-> () const
    {
        return getObjectPtr ();
    }

    inline ObjectType& operator* () const
    {
        return getObject ();
    }

    inline operator ObjectType* () const
    {
        return getObjectPtr ();
    }

    // TODO: Crashes on iOS if not accessed before usage
    static char s_storage [sizeof (ObjectType)];

private:
};

template <class ObjectType, class Tag>
char Storage <ObjectType, Tag>::s_storage [sizeof (ObjectType)];

}

//------------------------------------------------------------------------------

namespace detail
{

extern void staticObjectWait (std::size_t n);

}

/** Wrapper to produce an object with static storage duration.
    
    The object is constructed in a thread-safe fashion when the get function
    is first called. Note that the destructor for Object is never called. To
    invoke the destructor, use the AtExitHook facility (with caution).

    The Tag parameter allows multiple instances of the same Object type, by
    using different tags.

    Object must meet these requirements:
        DefaultConstructible

    @see AtExitHook
*/
template <class Object, typename Tag = void>
class StaticObject
{
public:
    static Object& get ()
    {
        StaticData& staticData (StaticData::get());

        if (staticData.state.get() != initialized)
        {
            if (staticData.state.compareAndSetBool (initializing, uninitialized))
            {
                // Initialize the object.
                new (&staticData.object) Object;
                staticData.state = initialized;
            }
            else
            {
                for (std::size_t n = 0; staticData.state.get() != initialized; ++n)
                {
                    detail::staticObjectWait (n);
                }
            }
        }

        return staticData.object;
    }

private:
    enum
    {
        uninitialized = 0,          // must be zero to function properly
        initializing,
        initialized
    };

    // This structure gets zero-filled at static initialization time.
    // No constructors are called.
    //
    class StaticData : public Uncopyable
    {
    public:
        Atomic <int> state;
        Object object;

        static StaticData& get ()
        {
            static uint8 storage [sizeof (StaticData)];
            return *(reinterpret_cast <StaticData*> (&storage [0]));
        }

    private:
        StaticData();
        ~StaticData();
    };
};

#endif
