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

#ifndef BEAST_SHAREDOBJECT_H_INCLUDED
#define BEAST_SHAREDOBJECT_H_INCLUDED

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

    This class uses an Atomic<int> value to hold the reference count, so that it
    the pointers can be passed between threads safely. For a faster but non-thread-safe
    version, use SingleThreadedSharedObject instead.

    @see SharedPtr, SharedObjectArray, SingleThreadedSharedObject
*/
class BEAST_API SharedObject : public Uncopyable
{
public:
    //==============================================================================
    /** Increments the object's reference count.

        This is done automatically by the smart pointer, but is public just
        in case it's needed for nefarious purposes.
    */
    inline void incReferenceCount() const noexcept
    {
        ++refCount;
    }

    /** Decreases the object's reference count.

        If doDelete is true the object will be deleted when the reference
        count drops to zero. The delete is performed using the regular
        operator and does NOT go through the ContainerDeletePolicy.

        The return value indicates if the reference count dropped to zero,
        so callers who know the derived type can use the ContainerDeletePolicy.
    */
    void decReferenceCount () const
    {
        bassert (getReferenceCount() > 0);
        if (--refCount == 0)
            destroy ();
    }

    /** Returns the object's current reference count. */
    inline int getReferenceCount() const noexcept
    {
        return refCount.get();
    }

protected:
    //==============================================================================
    /** Creates the reference-counted object (with an initial ref count of zero). */
    SharedObject()
    {
    }

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
        refCount = 0;
    }

private:
    //==============================================================================
    Atomic <int> mutable refCount;
};

//==============================================================================
/**
    Adds reference-counting to an object.

    This is effectively a version of the SharedObject class, but which
    uses a non-atomic counter, and so is not thread-safe (but which will be more
    efficient).
    For more details on how to use it, see the SharedObject class notes.

    @see SharedObject, SharedPtr, SharedObjectArray
*/
class BEAST_API SingleThreadedSharedObject : public Uncopyable
{
public:
    //==============================================================================
    /** Increments the object's reference count.

        This is done automatically by the smart pointer, but is public just
        in case it's needed for nefarious purposes.
    */
    inline void incReferenceCount() noexcept
    {
        ++refCount;
    }

    /** Decreases the object's reference count.

        If doDelete is true the object will be deleted when the reference
        count drops to zero. The delete is performed using the regular
        operator and does NOT go through the ContainerDeletePolicy.

        The return value indicates if the reference count dropped to zero,
        so callers who know the derived type can use the ContainerDeletePolicy.
    */
    inline void decReferenceCount ()
    {
        bassert (getReferenceCount() > 0);
        if (--refCount == 0)
            destroy ();
    }

    /** Returns the object's current reference count. */
    inline int getReferenceCount() const noexcept       { return refCount; }


protected:
    //==============================================================================
    /** Creates the reference-counted object (with an initial ref count of zero). */
    SingleThreadedSharedObject() : refCount (0)  {}

    /** Destructor. */
    virtual ~SingleThreadedSharedObject()
    {
        // it's dangerous to delete an object that's still referenced by something else!
        bassert (getReferenceCount() == 0);
    }

    virtual void destroy () const
    {
        delete this;
    }

private:
    //==============================================================================
    int refCount;
};

#endif
