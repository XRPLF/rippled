//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_SMART_PTR_SHAREDPTR_H_INCLUDED
#define BEAST_SMART_PTR_SHAREDPTR_H_INCLUDED

#include <ripple/beast/core/Config.h>
#include <ripple/beast/core/SharedObject.h>

namespace beast {

// Visual Studio doesn't seem to do very well when it comes
// to templated constructors and assignments so we provide
// non-templated versions when U and T are the same type.
//
#ifndef BEAST_SHAREDPTR_PROVIDE_COMPILER_WORKAROUNDS
#define BEAST_SHAREDPTR_PROVIDE_COMPILER_WORKAROUNDS 1
#endif

/** A smart-pointer container.

    Requirement:
        T must support the SharedObject concept.

    The template parameter specifies the class of the object you want to point
    to - the easiest way to make a class reference-countable is to simply make
    it inherit from SharedObject, but if you need to, you could roll your own
    reference-countable class by implementing a pair of methods called
    incReferenceCount() and decReferenceCount().

    When using this class, you'll probably want to create a using MyClassPtr = to
    abbreviate the full templated name - e.g.

    @code

    using MyClassPtr = SharedPtr <MyClass>;

    @endcode

    @see SharedObject, SharedObjectArray
*/
template <class T>
class SharedPtr
{
public:
    using value_type = T;

    /** The class being referenced by this container. */
    using ReferencedType = T;

    /** Construct a container pointing to nothing. */
    SharedPtr () noexcept
        : m_p (nullptr)
    {
    }

    /** Construct a container holding an object.
        This will increment the object's reference-count if it is non-null.
        Requirement:
            U* must be convertible to T*
    */
    /** @{ */
    SharedPtr (T* t) noexcept
        : m_p (acquire (t))
    {
    }

    template <class U>
    SharedPtr (U* u) noexcept
        : m_p (acquire (u))
    {
    }
    /** @} */

    /** Construct a container holding an object from another container.
        This will increment the object's reference-count (if it is non-null).
        Requirement:
            U* must be convertible to T*
    */
    /** @{ */
#if BEAST_SHAREDPTR_PROVIDE_COMPILER_WORKAROUNDS
    SharedPtr (SharedPtr const& sp) noexcept
        : m_p (acquire (sp.get ()))
    {
    }
#endif

    template <class U>
    SharedPtr (SharedPtr <U> const& sp) noexcept
        : m_p (acquire (sp.get ()))
    {
    }
    /** @} */

    /** Assign a different object to the container.
        The previous object beind held, if any, loses a reference count and
        will be destroyed if it is the last reference.
        Requirement:
            U* must be convertible to T*
    */
    /** @{ */
#if BEAST_SHAREDPTR_PROVIDE_COMPILER_WORKAROUNDS
    SharedPtr& operator= (T* t)
    {
        return assign (t);
    }
#endif

    template <class U>
    SharedPtr& operator= (U* u)
    {
        return assign (u);
    }
    /** @} */

    /** Assign an object from another container to this one. */
    /** @{ */
    SharedPtr& operator= (SharedPtr const& sp)
    {
        return assign (sp.get ());
    }

    /** Assign an object from another container to this one. */
    template <class U>
    SharedPtr& operator= (SharedPtr <U> const& sp)
    {
        return assign (sp.get ());
    }
    /** @} */

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    /** Construct a container with an object transferred from another container.
        The originating container loses its reference to the object.
        Requires:
            U* must be convertible to T*
    */
    /** @{ */
#if BEAST_SHAREDPTR_PROVIDE_COMPILER_WORKAROUNDS
    SharedPtr (SharedPtr && sp) noexcept
        : m_p (sp.swap <T> (nullptr))
    {
    }
#endif

    template <class U>
    SharedPtr (SharedPtr <U>&& sp) noexcept
        : m_p (sp.template swap <U> (nullptr))
    {
    }
    /** @} */

    /** Transfer ownership of another object to this container.
        The originating container loses its reference to the object.
        Requires:
            U* must be convertible to T*
    */
    /** @{ */
#if BEAST_SHAREDPTR_PROVIDE_COMPILER_WORKAROUNDS
    SharedPtr& operator= (SharedPtr && sp) noexcept
    {
        return assign (sp.swap <T> (nullptr));
    }
#endif

    template <class U>
    SharedPtr& operator= (SharedPtr <U>&& sp)
    {
        return assign (sp.template swap <U> (nullptr));
    }
    /** @} */
#endif

    /** Destroy the container and release the held reference, if any.
    */
    ~SharedPtr ()
    {
        release (m_p);
    }

    /** Returns `true` if the container is not pointing to an object. */
    bool empty () const noexcept
    {
        return m_p == nullptr;
    }

    /** Returns the object that this pointer references if any, else nullptr. */
    operator T* () const noexcept
    {
        return m_p;
    }

    /** Returns the object that this pointer references if any, else nullptr. */
    T* operator-> () const noexcept
    {
        return m_p;
    }

    /** Returns the object that this pointer references if any, else nullptr. */
    T* get () const noexcept
    {
        return m_p;
    }

private:
    // Acquire a reference to u for the caller.
    //
    template <class U>
    static T* acquire (U* u) noexcept
    {
        if (u != nullptr)
            u->incReferenceCount ();
        return u;
    }

    static void release (T* t)
    {
        if (t != nullptr)
            t->decReferenceCount ();
    }

    // Swap ownership of the currently referenced object.
    // The caller receives a pointer to the original object,
    // and this container is left with the passed object. No
    // reference counts are changed.
    //
    template <class U>
    T* swap (U* u)
    {
        T* const t (m_p);
        m_p = u;
        return t;
    }

    // Acquire ownership of u.
    // Any previous reference is released.
    //
    template <class U>
    SharedPtr& assign (U* u)
    {
        if (m_p != u)
            release (this->swap (acquire (u)));
        return *this;
    }

    T* m_p;
};

//------------------------------------------------------------------------------

// bind() helpers for pointer to member function

template <class T>
const T* get_pointer (SharedPtr<T> const& ptr)
{
    return ptr.get();
}

template <class T>
T* get_pointer (SharedPtr<T>& ptr)
{
    return ptr.get();
}

//------------------------------------------------------------------------------

/** SharedPtr comparisons. */
/** @{ */
template <class T, class U>
bool operator== (SharedPtr <T> const& lhs, U* const rhs) noexcept
{
    return lhs.get() == rhs;
}

template <class T, class U>
bool operator== (SharedPtr <T> const& lhs, SharedPtr <U> const& rhs) noexcept
{
    return lhs.get() == rhs.get();
}

template <class T, class U>
bool operator== (T const* lhs, SharedPtr <U> const& rhs) noexcept
{
    return lhs == rhs.get();
}

template <class T, class U>
bool operator!= (SharedPtr <T> const& lhs, U const* rhs) noexcept
{
    return lhs.get() != rhs;
}

template <class T, class U>
bool operator!= (SharedPtr <T> const& lhs, SharedPtr <U> const& rhs) noexcept
{
    return lhs.get() != rhs.get();
}

template <class T, class U>
bool operator!= (T const* lhs, SharedPtr <U> const& rhs) noexcept
{
    return lhs != rhs.get();
}
/** @} */

}

#endif
