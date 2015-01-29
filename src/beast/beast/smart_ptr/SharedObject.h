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

#ifndef BEAST_SMART_PTR_SHAREDOBJECT_H_INCLUDED
#define BEAST_SMART_PTR_SHAREDOBJECT_H_INCLUDED

#include <atomic>

#include <beast/Config.h>

namespace beast {

//==============================================================================
/**
    Adds reference-counting to an object.

    To add reference-counting to a class, derive it from this class, and
    use the SharedPtr class to point to it.

    e.g. @code
    class MyClass : public SharedObject
    {
        void foo();

        // This is a neat way of declaring a typedef for a pointer class,
        // rather than typing out the full templated name each time..
        typedef SharedPtr<MyClass> Ptr;
    };

    MyClass::Ptr p = new MyClass();
    MyClass::Ptr p2 = p;
    p = nullptr;
    p2->foo();
    @endcode

    Once a new SharedObject has been assigned to a pointer, be
    careful not to delete the object manually.

    This class uses an std::atomic<int> value to hold the reference count, so
    that the pointers can be passed between threads safely.

    @see SharedPtr, SharedObjectArray
*/
class SharedObject
{
public:
    //==============================================================================
    /** Increments the object's reference count.

        This is done automatically by the smart pointer, but is public just
        in case it's needed for nefarious purposes.
    */
    void incReferenceCount() const noexcept
    {
        ++refCount;
    }

    /** Decreases the object's reference count. */
    void decReferenceCount () const
    {
        bassert (getReferenceCount() > 0);
        if (--refCount == 0)
            destroy ();
    }

    /** Returns the object's current reference count. */
    int getReferenceCount() const noexcept
    {
        return refCount.load();
    }

protected:
    //==============================================================================
    /** Creates the reference-counted object (with an initial ref count of zero). */
    SharedObject()
        : refCount (0)
    {
    }

    SharedObject (SharedObject const&) = delete;
    SharedObject& operator= (SharedObject const&) = delete;

    /** Destructor. */
    virtual ~SharedObject()
    {
        // it's dangerous to delete an object that's still referenced by something else!
        bassert (getReferenceCount() == 0);
    }

    /** Destroy the object.
        Derived classes can override this for different behaviors.
    */
    virtual void destroy () const
    {
        delete this;
    }

    /** Resets the reference count to zero without deleting the object.
        You should probably never need to use this!
    */
    void resetReferenceCount() noexcept
    {
        refCount.store (0);
    }

private:
    //==============================================================================
    std::atomic <int> mutable refCount;
};

}

#endif
