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

#ifndef BEAST_REFERENCECOUNTEDOBJECT_BEASTHEADER
#define BEAST_REFERENCECOUNTEDOBJECT_BEASTHEADER

#include "beast_Atomic.h"


//==============================================================================
/**
    Adds reference-counting to an object.

    To add reference-counting to a class, derive it from this class, and
    use the SharedObjectPtr class to point to it.

    e.g. @code
    class MyClass : public SharedObject
    {
        void foo();

        // This is a neat way of declaring a typedef for a pointer class,
        // rather than typing out the full templated name each time..
        typedef SharedObjectPtr<MyClass> Ptr;
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

    @see SharedObjectPtr, SharedObjectArray, SingleThreadedSharedObject
*/
class BEAST_API SharedObject : public Uncopyable
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

        If the count gets to zero, the object will be deleted.
    */
    inline void decReferenceCount() noexcept
    {
        bassert (getReferenceCount() > 0);

        if (--refCount == 0)
            delete this;
    }

    /** Returns the object's current reference count. */
    inline int getReferenceCount() const noexcept       { return refCount.get(); }


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

    /** Resets the reference count to zero without deleting the object.
        You should probably never need to use this!
    */
    void resetReferenceCount() noexcept
    {
        refCount = 0;
    }

private:
    //==============================================================================
    Atomic <int> refCount;
};


//==============================================================================
/**
    Adds reference-counting to an object.

    This is effectively a version of the SharedObject class, but which
    uses a non-atomic counter, and so is not thread-safe (but which will be more
    efficient).
    For more details on how to use it, see the SharedObject class notes.

    @see SharedObject, SharedObjectPtr, SharedObjectArray
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

        If the count gets to zero, the object will be deleted.
    */
    inline void decReferenceCount() noexcept
    {
        bassert (getReferenceCount() > 0);

        if (--refCount == 0)
            delete this;
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

private:
    //==============================================================================
    int refCount;
};


//==============================================================================
/**
    A smart-pointer class which points to a reference-counted object.

    The template parameter specifies the class of the object you want to point to - the easiest
    way to make a class reference-countable is to simply make it inherit from SharedObject,
    but if you need to, you could roll your own reference-countable class by implementing a pair of
    mathods called incReferenceCount() and decReferenceCount().

    When using this class, you'll probably want to create a typedef to abbreviate the full
    templated name - e.g.
    @code typedef SharedObjectPtr<MyClass> MyClassPtr;@endcode

    @see SharedObject, SharedObjectArray
*/
template <class SharedObjectClass>
class SharedObjectPtr
{
public:
    /** The class being referenced by this pointer. */
    typedef SharedObjectClass ReferencedType;

    //==============================================================================
    /** Creates a pointer to a null object. */
    inline SharedObjectPtr() noexcept
        : referencedObject (nullptr)
    {
    }

    /** Creates a pointer to an object.

        This will increment the object's reference-count if it is non-null.
    */
    inline SharedObjectPtr (SharedObjectClass* const refCountedObject) noexcept
        : referencedObject (refCountedObject)
    {
        if (refCountedObject != nullptr)
            refCountedObject->incReferenceCount();
    }

    /** Copies another pointer.
        This will increment the object's reference-count (if it is non-null).
    */
    inline SharedObjectPtr (const SharedObjectPtr& other) noexcept
        : referencedObject (other.referencedObject)
    {
        if (referencedObject != nullptr)
            referencedObject->incReferenceCount();
    }

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    /** Takes-over the object from another pointer. */
    inline SharedObjectPtr (SharedObjectPtr&& other) noexcept
        : referencedObject (other.referencedObject)
    {
        other.referencedObject = nullptr;
    }
   #endif

    /** Copies another pointer.
        This will increment the object's reference-count (if it is non-null).
    */
    template <class DerivedClass>
    inline SharedObjectPtr (const SharedObjectPtr<DerivedClass>& other) noexcept
        : referencedObject (static_cast <SharedObjectClass*> (other.get()))
    {
        if (referencedObject != nullptr)
            referencedObject->incReferenceCount();
    }

    /** Changes this pointer to point at a different object.

        The reference count of the old object is decremented, and it might be
        deleted if it hits zero. The new object's count is incremented.
    */
    SharedObjectPtr& operator= (const SharedObjectPtr& other)
    {
        return operator= (other.referencedObject);
    }

    /** Changes this pointer to point at a different object.

        The reference count of the old object is decremented, and it might be
        deleted if it hits zero. The new object's count is incremented.
    */
    template <class DerivedClass>
    SharedObjectPtr& operator= (const SharedObjectPtr<DerivedClass>& other)
    {
        return operator= (static_cast <SharedObjectClass*> (other.get()));
    }

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    /** Takes-over the object from another pointer. */
    SharedObjectPtr& operator= (SharedObjectPtr&& other)
    {
        std::swap (referencedObject, other.referencedObject);
        return *this;
    }
   #endif

    /** Changes this pointer to point at a different object.

        The reference count of the old object is decremented, and it might be
        deleted if it hits zero. The new object's count is incremented.
    */
    SharedObjectPtr& operator= (SharedObjectClass* const newObject)
    {
        if (referencedObject != newObject)
        {
            if (newObject != nullptr)
                newObject->incReferenceCount();

            SharedObjectClass* const oldObject = referencedObject;
            referencedObject = newObject;

            if (oldObject != nullptr)
                oldObject->decReferenceCount();
        }

        return *this;
    }

    /** Destructor.

        This will decrement the object's reference-count, and may delete it if it
        gets to zero.
    */
    inline ~SharedObjectPtr()
    {
        if (referencedObject != nullptr)
            referencedObject->decReferenceCount();
    }

    /** Returns the object that this pointer references.
        The pointer returned may be zero, of course.
    */
    inline operator SharedObjectClass*() const noexcept
    {
        return referencedObject;
    }

    // the -> operator is called on the referenced object
    inline SharedObjectClass* operator->() const noexcept
    {
        return referencedObject;
    }

    /** Returns the object that this pointer references.
        The pointer returned may be zero, of course.
    */
    inline SharedObjectClass* get() const noexcept
    {
        return referencedObject;
    }

    /** Returns the object that this pointer references.
        The pointer returned may be zero, of course.
    */
    inline SharedObjectClass* getObject() const noexcept
    {
        return referencedObject;
    }

private:
    //==============================================================================
    SharedObjectClass* referencedObject;
};


/** Compares two SharedObjectPointers. */
template <class SharedObjectClass>
bool operator== (const SharedObjectPtr<SharedObjectClass>& object1, SharedObjectClass* const object2) noexcept
{
    return object1.get() == object2;
}

/** Compares two SharedObjectPointers. */
template <class SharedObjectClass>
bool operator== (const SharedObjectPtr<SharedObjectClass>& object1, const SharedObjectPtr<SharedObjectClass>& object2) noexcept
{
    return object1.get() == object2.get();
}

/** Compares two SharedObjectPointers. */
template <class SharedObjectClass>
bool operator== (SharedObjectClass* object1, SharedObjectPtr<SharedObjectClass>& object2) noexcept
{
    return object1 == object2.get();
}

/** Compares two SharedObjectPointers. */
template <class SharedObjectClass>
bool operator!= (const SharedObjectPtr<SharedObjectClass>& object1, const SharedObjectClass* object2) noexcept
{
    return object1.get() != object2;
}

/** Compares two SharedObjectPointers. */
template <class SharedObjectClass>
bool operator!= (const SharedObjectPtr<SharedObjectClass>& object1, SharedObjectPtr<SharedObjectClass>& object2) noexcept
{
    return object1.get() != object2.get();
}

/** Compares two SharedObjectPointers. */
template <class SharedObjectClass>
bool operator!= (SharedObjectClass* object1, SharedObjectPtr<SharedObjectClass>& object2) noexcept
{
    return object1 != object2.get();
}


#endif   // BEAST_REFERENCECOUNTEDOBJECT_BEASTHEADER
