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

#ifndef BEAST_MODULE_CORE_CONTAINERS_ARRAYALLOCATIONBASE_H_INCLUDED
#define BEAST_MODULE_CORE_CONTAINERS_ARRAYALLOCATIONBASE_H_INCLUDED

#include <beast/HeapBlock.h>

namespace beast {

//==============================================================================
/**
    Implements some basic array storage allocation functions.

    This class isn't really for public use - it's used by the other
    array classes, but might come in handy for some purposes.

    It inherits from a critical section class to allow the arrays to use
    the "empty base class optimisation" pattern to reduce their footprint.

    @see Array, SharedObjectArray
*/
template <class ElementType, class TypeOfCriticalSectionToUse>
class ArrayAllocationBase
    : public TypeOfCriticalSectionToUse
{
public:
    //==============================================================================
    /** Creates an empty array. */
    ArrayAllocationBase() noexcept
        : numAllocated (0)
    {
    }

    /** Destructor. */
    ~ArrayAllocationBase() noexcept
    {
    }

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    ArrayAllocationBase (ArrayAllocationBase<ElementType, TypeOfCriticalSectionToUse>&& other) noexcept
        : elements (static_cast <HeapBlock <ElementType>&&> (other.elements)),
          numAllocated (other.numAllocated)
    {
    }

    ArrayAllocationBase& operator= (ArrayAllocationBase<ElementType, TypeOfCriticalSectionToUse>&& other) noexcept
    {
        elements = static_cast <HeapBlock <ElementType>&&> (other.elements);
        numAllocated = other.numAllocated;
        return *this;
    }
   #endif

    //==============================================================================
    /** Changes the amount of storage allocated.

        This will retain any data currently held in the array, and either add or
        remove extra space at the end.

        @param numElements  the number of elements that are needed
    */
    void setAllocatedSize (const int numElements)
    {
        if (numAllocated != numElements)
        {
            if (numElements > 0)
                elements.reallocate ((size_t) numElements);
            else
                elements.free_up();

            numAllocated = numElements;
        }
    }

    /** Increases the amount of storage allocated if it is less than a given amount.

        This will retain any data currently held in the array, but will add
        extra space at the end to make sure there it's at least as big as the size
        passed in. If it's already bigger, no action is taken.

        @param minNumElements  the minimum number of elements that are needed
    */
    void ensureAllocatedSize (const int minNumElements)
    {
        if (minNumElements > numAllocated)
            setAllocatedSize ((minNumElements + minNumElements / 2 + 8) & ~7);

        bassert (numAllocated <= 0 || elements != nullptr);
    }

    /** Minimises the amount of storage allocated so that it's no more than
        the given number of elements.
    */
    void shrinkToNoMoreThan (const int maxNumElements)
    {
        if (maxNumElements < numAllocated)
            setAllocatedSize (maxNumElements);
    }

    /** Swap the contents of two objects. */
    void swapWith (ArrayAllocationBase <ElementType, TypeOfCriticalSectionToUse>& other) noexcept
    {
        elements.swapWith (other.elements);
        std::swap (numAllocated, other.numAllocated);
    }

    //==============================================================================
    HeapBlock <ElementType> elements;
    int numAllocated;
};

} // beast

#endif   // BEAST_ARRAYALLOCATIONBASE_H_INCLUDED
