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

#ifndef BEAST_STL_UNIQUE_PTR_H_INCLUDED
#define BEAST_STL_UNIQUE_PTR_H_INCLUDED

#include "../Config.h"
#include "../Uncopyable.h"

#ifndef BEAST_UNIQUE_PTR_USES_STL
# if BEAST_USE_CPLUSPLUS11
#  define BEAST_UNIQUE_PTR_USES_STL 1
# else
#  define BEAST_UNIQUE_PTR_USES_STL 0
# endif
#endif

#if BEAST_UNIQUE_PTR_USES_STL
# include <memory>
namespace beast {
using std::unique_ptr;
}

#else

namespace beast {

template <class ObjectType>
class unique_ptr : public Uncopyable
{
public:
    //==============================================================================
    /** Creates a unique_ptr containing a null pointer. */
    inline unique_ptr()
        : object (nullptr)
    {
    }

    /** Creates a unique_ptr that owns the specified object. */
    inline unique_ptr (ObjectType* const objectToTakePossessionOf)
        : object (objectToTakePossessionOf)
    {
    }

    /** Creates a unique_ptr that takes its pointer from another unique_ptr.

        Because a pointer can only belong to one unique_ptr, this transfers
        the pointer from the other object to this one, and the other object is reset to
        be a null pointer.
    */
    unique_ptr (unique_ptr& objectToTransferFrom)
        : object (objectToTransferFrom.object)
    {
        objectToTransferFrom.object = nullptr;
    }

    /** Destructor.
        This will delete the object that this unique_ptr currently refers to.
    */
    inline ~unique_ptr()
    {
        ContainerDeletePolicy <ObjectType>::destroy (object);
    }

    /** Changes this unique_ptr to point to a new object.

        Because a pointer can only belong to one unique_ptr, this transfers
        the pointer from the other object to this one, and the other object is reset to
        be a null pointer.

        If this unique_ptr already points to an object, that object
        will first be deleted.
    */
    unique_ptr& operator= (unique_ptr& objectToTransferFrom)
    {
        if (this != objectToTransferFrom.getAddress())
        {
            // Two ScopedPointers should never be able to refer to the same object - if
            // this happens, you must have done something dodgy!
            bassert (object == nullptr || object != objectToTransferFrom.object);

            ObjectType* const oldObject = object;
            object = objectToTransferFrom.object;
            objectToTransferFrom.object = nullptr;
            ContainerDeletePolicy <ObjectType>::destroy (oldObject);
        }

        return *this;
    }

    /** Changes this unique_ptr to point to a new object.

        If this unique_ptr already points to an object, that object
        will first be deleted.

        The pointer that you pass in may be a nullptr.
    */
    unique_ptr& operator= (ObjectType* const newObjectToTakePossessionOf)
    {
        if (object != newObjectToTakePossessionOf)
        {
            ObjectType* const oldObject = object;
            object = newObjectToTakePossessionOf;
            ContainerDeletePolicy <ObjectType>::destroy (oldObject);
        }

        return *this;
    }

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    unique_ptr (unique_ptr&& other)
        : object (other.object)
    {
        other.object = nullptr;
    }

    unique_ptr& operator= (unique_ptr&& other)
    {
        object = other.object;
        other.object = nullptr;
        return *this;
    }
   #endif

    //==============================================================================
    /** Returns the object that this unique_ptr refers to. */
    inline operator ObjectType*() const                                    { return object; }

    /** Returns the object that this unique_ptr refers to. */
    inline ObjectType* get() const                                         { return object; }

    /** Returns the object that this unique_ptr refers to. */
    inline ObjectType& operator*() const                                   { return *object; }

    /** Lets you access methods and properties of the object that this unique_ptr refers to. */
    inline ObjectType* operator->() const                                  { return object; }

    //==============================================================================
    /** Removes the current object from this unique_ptr without deleting it.
        This will return the current object, and set the unique_ptr to a null pointer.
    */
    ObjectType* release()                                                  { ObjectType* const o = object; object = nullptr; return o; }

    //==============================================================================
    /** Swaps this object with that of another unique_ptr.
        The two objects simply exchange their pointers.
    */
    void swapWith (unique_ptr <ObjectType>& other)
    {
        // Two ScopedPointers should never be able to refer to the same object - if
        // this happens, you must have done something dodgy!
        bassert (object != other.object || this == other.getAddress());
        std::swap (object, other.object);
    }

    inline ObjectType* createCopy() const                                           { return createCopyIfNotNull (object); }

private:
    ObjectType* object;
    const unique_ptr* getAddress() const                                { return this; }
};

template <class ObjectType>
bool operator== (const unique_ptr<ObjectType>& pointer1, ObjectType* const pointer2)
{
    return static_cast <ObjectType*> (pointer1) == pointer2;
}

template <class ObjectType>
bool operator!= (const unique_ptr<ObjectType>& pointer1, ObjectType* const pointer2)
{
    return static_cast <ObjectType*> (pointer1) != pointer2;
}

//------------------------------------------------------------------------------

}

#endif

namespace beast {

template <class T>
unique_ptr <T> make_unique ()
{
    return unique_ptr <T> (new T ());
}

template <class T, class P1>
unique_ptr <T> make_unique (P1 p1)
{
    return unique_ptr <T> (new T (p1));
}

template <class T, class P1, class P2>
unique_ptr <T> make_unique (P1 p1, P2 p2)
{
    return unique_ptr <T> (new T (p1, p2));
}

template <class T, class P1, class P2, class P3>
unique_ptr <T> make_unique (P1 p1, P2 p2, P3 p3)
{
    return unique_ptr <T> (new T (p1, p2, p3));
}

template <class T, class P1, class P2, class P3, class P4>
unique_ptr <T> make_unique (P1 p1, P2 p2, P3 p3, P4 p4)
{
    return unique_ptr <T> (new T (p1, p2, p3, p4));
}

template <class T, class P1, class P2, class P3, class P4, class P5>
unique_ptr <T> make_unique (P1 p1, P2 p2, P3 p3, P4 p4, P5 p5)
{
    return unique_ptr <T> (new T (p1, p2, p3, p4, p5));
}

template <class T, class P1, class P2, class P3, class P4, class P5, class P6>
unique_ptr <T> make_unique (P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6)
{
    return unique_ptr <T> (new T (p1, p2, p3, p4, p5, p6));
}

template <class T, class P1, class P2, class P3, class P4, class P5, class P6, class P7>
unique_ptr <T> make_unique (P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7)
{
    return unique_ptr <T> (new T (p1, p2, p3, p4, p5, p6, p7));
}

template <class T, class P1, class P2, class P3, class P4, class P5, class P6, class P7, class P8>
unique_ptr <T> make_unique (P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8)
{
    return unique_ptr <T> (new T (p1, p2, p3, p4, p5, p6, p7, p8));
}

}

#endif
