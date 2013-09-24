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

#ifndef BEAST_ARRAY_H_INCLUDED
#define BEAST_ARRAY_H_INCLUDED

//==============================================================================
/**
    Holds a resizable array of primitive or copy-by-value objects.

    Examples of arrays are: Array<int>, Array<Rectangle> or Array<MyClass*>

    The Array class can be used to hold simple, non-polymorphic objects as well as primitive types - to
    do so, the class must fulfil these requirements:
    - it must have a copy constructor and assignment operator
    - it must be able to be relocated in memory by a memcpy without this causing any problems - so
      objects whose functionality relies on external pointers or references to themselves can be used.

    You can of course have an array of pointers to any kind of object, e.g. Array <MyClass*>, but if
    you do this, the array doesn't take any ownership of the objects - see the OwnedArray class or the
    SharedObjectArray class for more powerful ways of holding lists of objects.

    For holding lists of strings, you can use Array\<String\>, but it's usually better to use the
    specialised class StringArray, which provides more useful functions.

    To make all the array's methods thread-safe, pass in "CriticalSection" as the templated
    TypeOfCriticalSectionToUse parameter, instead of the default DummyCriticalSection.

    @see OwnedArray, SharedObjectArray, StringArray, CriticalSection
*/
template <typename ElementType,
          typename TypeOfCriticalSectionToUse = DummyCriticalSection,
          int minimumAllocatedSize = 0>
class Array
{
private:
    typedef PARAMETER_TYPE (ElementType) ParameterType;

public:
    //==============================================================================
    /** Creates an empty array. */
    Array() noexcept
       : numUsed (0)
    {
    }

    /** Creates a copy of another array.
        @param other    the array to copy
    */
    Array (const Array<ElementType, TypeOfCriticalSectionToUse>& other)
    {
        const ScopedLockType lock (other.getLock());
        numUsed = other.numUsed;
        data.setAllocatedSize (other.numUsed);

        for (int i = 0; i < numUsed; ++i)
            new (data.elements + i) ElementType (other.data.elements[i]);
    }

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    Array (Array<ElementType, TypeOfCriticalSectionToUse>&& other) noexcept
        : data (static_cast <ArrayAllocationBase<ElementType, TypeOfCriticalSectionToUse>&&> (other.data)),
          numUsed (other.numUsed)
    {
        other.numUsed = 0;
    }
   #endif

    /** Initalises from a null-terminated C array of values.

        @param values   the array to copy from
    */
    template <typename TypeToCreateFrom>
    explicit Array (const TypeToCreateFrom* values)
       : numUsed (0)
    {
        while (*values != TypeToCreateFrom())
            add (*values++);
    }

    /** Initalises from a C array of values.

        @param values       the array to copy from
        @param numValues    the number of values in the array
    */
    template <typename TypeToCreateFrom>
    Array (const TypeToCreateFrom* values, int numValues)
       : numUsed (numValues)
    {
        data.setAllocatedSize (numValues);

        for (int i = 0; i < numValues; ++i)
            new (data.elements + i) ElementType (values[i]);
    }

    /** Destructor. */
    ~Array()
    {
        deleteAllElements();
    }

    /** Copies another array.
        @param other    the array to copy
    */
    Array& operator= (const Array& other)
    {
        if (this != &other)
        {
            Array<ElementType, TypeOfCriticalSectionToUse> otherCopy (other);
            swapWith (otherCopy);
        }

        return *this;
    }

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    Array& operator= (Array&& other) noexcept
    {
        const ScopedLockType lock (getLock());
        data = static_cast <ArrayAllocationBase<ElementType, TypeOfCriticalSectionToUse>&&> (other.data);
        numUsed = other.numUsed;
        other.numUsed = 0;
        return *this;
    }
   #endif

    //==============================================================================
    /** Compares this array to another one.
        Two arrays are considered equal if they both contain the same set of
        elements, in the same order.
        @param other    the other array to compare with
    */
    template <class OtherArrayType>
    bool operator== (const OtherArrayType& other) const
    {
        const ScopedLockType lock (getLock());
        const typename OtherArrayType::ScopedLockType lock2 (other.getLock());

        if (numUsed != other.numUsed)
            return false;

        for (int i = numUsed; --i >= 0;)
            if (! (data.elements [i] == other.data.elements [i]))
                return false;

        return true;
    }

    /** Compares this array to another one.
        Two arrays are considered equal if they both contain the same set of
        elements, in the same order.
        @param other    the other array to compare with
    */
    template <class OtherArrayType>
    bool operator!= (const OtherArrayType& other) const
    {
        return ! operator== (other);
    }

    //==============================================================================
    /** Removes all elements from the array.
        This will remove all the elements, and free any storage that the array is
        using. To clear the array without freeing the storage, use the clearQuick()
        method instead.

        @see clearQuick
    */
    void clear()
    {
        const ScopedLockType lock (getLock());
        deleteAllElements();
        data.setAllocatedSize (0);
        numUsed = 0;
    }

    /** Removes all elements from the array without freeing the array's allocated storage.

        @see clear
    */
    void clearQuick()
    {
        const ScopedLockType lock (getLock());
        deleteAllElements();
        numUsed = 0;
    }

    //==============================================================================
    /** Returns the current number of elements in the array.
    */
    inline int size() const noexcept
    {
        return numUsed;
    }

    /** Returns one of the elements in the array.
        If the index passed in is beyond the range of valid elements, this
        will return a default value.

        If you're certain that the index will always be a valid element, you
        can call getUnchecked() instead, which is faster.

        @param index    the index of the element being requested (0 is the first element in the array)
        @see getUnchecked, getFirst, getLast
    */
    ElementType operator[] (const int index) const
    {
        const ScopedLockType lock (getLock());

        if (isPositiveAndBelow (index, numUsed))
        {
            bassert (data.elements != nullptr);
            return data.elements [index];
        }

        return ElementType();
    }

    /** Returns one of the elements in the array, without checking the index passed in.

        Unlike the operator[] method, this will try to return an element without
        checking that the index is within the bounds of the array, so should only
        be used when you're confident that it will always be a valid index.

        @param index    the index of the element being requested (0 is the first element in the array)
        @see operator[], getFirst, getLast
    */
    inline ElementType getUnchecked (const int index) const
    {
        const ScopedLockType lock (getLock());
        bassert (isPositiveAndBelow (index, numUsed) && data.elements != nullptr);
        return data.elements [index];
    }

    /** Returns a direct reference to one of the elements in the array, without checking the index passed in.

        This is like getUnchecked, but returns a direct reference to the element, so that
        you can alter it directly. Obviously this can be dangerous, so only use it when
        absolutely necessary.

        @param index    the index of the element being requested (0 is the first element in the array)
        @see operator[], getFirst, getLast
    */
    inline ElementType& getReference (const int index) const noexcept
    {
        const ScopedLockType lock (getLock());
        bassert (isPositiveAndBelow (index, numUsed) && data.elements != nullptr);
        return data.elements [index];
    }

    /** Returns the first element in the array, or a default value if the array is empty.

        @see operator[], getUnchecked, getLast
    */
    inline ElementType getFirst() const
    {
        const ScopedLockType lock (getLock());
        return (numUsed > 0) ? data.elements [0]
                             : ElementType();
    }

    /** Returns the last element in the array, or a default value if the array is empty.

        @see operator[], getUnchecked, getFirst
    */
    inline ElementType getLast() const
    {
        const ScopedLockType lock (getLock());
        return (numUsed > 0) ? data.elements [numUsed - 1]
                             : ElementType();
    }

    /** Returns a pointer to the actual array data.
        This pointer will only be valid until the next time a non-const method
        is called on the array.
    */
    inline ElementType* getRawDataPointer() noexcept
    {
        return data.elements;
    }

    //==============================================================================
    /** Returns a pointer to the first element in the array.
        This method is provided for compatibility with standard C++ iteration mechanisms.
    */
    inline ElementType* begin() const noexcept
    {
        return data.elements;
    }

    /** Returns a pointer to the element which follows the last element in the array.
        This method is provided for compatibility with standard C++ iteration mechanisms.
    */
    inline ElementType* end() const noexcept
    {
        return data.elements + numUsed;
    }

    //==============================================================================
    /** Finds the index of the first element which matches the value passed in.

        This will search the array for the given object, and return the index
        of its first occurrence. If the object isn't found, the method will return -1.

        @param elementToLookFor   the value or object to look for
        @returns                  the index of the object, or -1 if it's not found
    */
    int indexOf (ParameterType elementToLookFor) const
    {
        const ScopedLockType lock (getLock());
        const ElementType* e = data.elements.getData();
        const ElementType* const end_ = e + numUsed;

        for (; e != end_; ++e)
            if (elementToLookFor == *e)
                return static_cast <int> (e - data.elements.getData());

        return -1;
    }

    /** Returns true if the array contains at least one occurrence of an object.

        @param elementToLookFor     the value or object to look for
        @returns                    true if the item is found
    */
    bool contains (ParameterType elementToLookFor) const
    {
        const ScopedLockType lock (getLock());
        const ElementType* e = data.elements.getData();
        const ElementType* const end_ = e + numUsed;

        for (; e != end_; ++e)
            if (elementToLookFor == *e)
                return true;

        return false;
    }

    //==============================================================================
    /** Appends a new element at the end of the array.

        @param newElement       the new object to add to the array
        @return The index of the new item, or -1 if it already existed.
        @see set, insert, addIfNotAlreadyThere, addSorted, addUsingDefaultSort, addArray
    */
    int add (ParameterType newElement)
    {
        const ScopedLockType lock (getLock());
        data.ensureAllocatedSize (numUsed + 1);
        new (data.elements + numUsed++) ElementType (newElement);
        return numUsed;
    }

    /** Inserts a new element into the array at a given position.

        If the index is less than 0 or greater than the size of the array, the
        element will be added to the end of the array.
        Otherwise, it will be inserted into the array, moving all the later elements
        along to make room.

        @param indexToInsertAt    the index at which the new element should be
                                  inserted (pass in -1 to add it to the end)
        @param newElement         the new object to add to the array
        @see add, addSorted, addUsingDefaultSort, set
    */
    void insert (int indexToInsertAt, ParameterType newElement)
    {
        const ScopedLockType lock (getLock());
        data.ensureAllocatedSize (numUsed + 1);
        bassert (data.elements != nullptr);

        if (isPositiveAndBelow (indexToInsertAt, numUsed))
        {
            ElementType* const insertPos = data.elements + indexToInsertAt;
            const int numberToMove = numUsed - indexToInsertAt;

            if (numberToMove > 0)
                memmove (insertPos + 1, insertPos, ((size_t) numberToMove) * sizeof (ElementType));

            new (insertPos) ElementType (newElement);
            ++numUsed;
        }
        else
        {
            new (data.elements + numUsed++) ElementType (newElement);
        }
    }

    /** Inserts multiple copies of an element into the array at a given position.

        If the index is less than 0 or greater than the size of the array, the
        element will be added to the end of the array.
        Otherwise, it will be inserted into the array, moving all the later elements
        along to make room.

        @param indexToInsertAt    the index at which the new element should be inserted
        @param newElement         the new object to add to the array
        @param numberOfTimesToInsertIt  how many copies of the value to insert
        @see insert, add, addSorted, set
    */
    void insertMultiple (int indexToInsertAt, ParameterType newElement,
                         int numberOfTimesToInsertIt)
    {
        if (numberOfTimesToInsertIt > 0)
        {
            const ScopedLockType lock (getLock());
            data.ensureAllocatedSize (numUsed + numberOfTimesToInsertIt);
            ElementType* insertPos;

            if (isPositiveAndBelow (indexToInsertAt, numUsed))
            {
                insertPos = data.elements + indexToInsertAt;
                const int numberToMove = numUsed - indexToInsertAt;
                memmove (insertPos + numberOfTimesToInsertIt, insertPos, ((size_t) numberToMove) * sizeof (ElementType));
            }
            else
            {
                insertPos = data.elements + numUsed;
            }

            numUsed += numberOfTimesToInsertIt;

            while (--numberOfTimesToInsertIt >= 0)
                new (insertPos++) ElementType (newElement);
        }
    }

    /** Inserts an array of values into this array at a given position.

        If the index is less than 0 or greater than the size of the array, the
        new elements will be added to the end of the array.
        Otherwise, they will be inserted into the array, moving all the later elements
        along to make room.

        @param indexToInsertAt      the index at which the first new element should be inserted
        @param newElements          the new values to add to the array
        @param numberOfElements     how many items are in the array
        @see insert, add, addSorted, set
    */
    void insertArray (int indexToInsertAt,
                      const ElementType* newElements,
                      int numberOfElements)
    {
        if (numberOfElements > 0)
        {
            const ScopedLockType lock (getLock());
            data.ensureAllocatedSize (numUsed + numberOfElements);
            ElementType* insertPos = data.elements;

            if (isPositiveAndBelow (indexToInsertAt, numUsed))
            {
                insertPos += indexToInsertAt;
                const int numberToMove = numUsed - indexToInsertAt;
                memmove (insertPos + numberOfElements, insertPos, numberToMove * sizeof (ElementType));
            }
            else
            {
                insertPos += numUsed;
            }

            numUsed += numberOfElements;

            while (--numberOfElements >= 0)
                new (insertPos++) ElementType (*newElements++);
        }
    }

    /** Appends a new element at the end of the array as long as the array doesn't
        already contain it.

        If the array already contains an element that matches the one passed in, nothing
        will be done.

        @param newElement   the new object to add to the array
        @return The index of the new item, or -1 if it already existed.
    */
    int addIfNotAlreadyThere (ParameterType newElement)
    {
        const ScopedLockType lock (getLock());

        if (! contains (newElement))
            return add (newElement);

        return -1;
    }

    /** Replaces an element with a new value.

        If the index is less than zero, this method does nothing.
        If the index is beyond the end of the array, the item is added to the end of the array.

        @param indexToChange    the index whose value you want to change
        @param newValue         the new value to set for this index.
        @see add, insert
    */
    void set (const int indexToChange, ParameterType newValue)
    {
        bassert (indexToChange >= 0);
        const ScopedLockType lock (getLock());

        if (isPositiveAndBelow (indexToChange, numUsed))
        {
            data.elements [indexToChange] = newValue;
        }
        else if (indexToChange >= 0)
        {
            data.ensureAllocatedSize (numUsed + 1);
            new (data.elements + numUsed++) ElementType (newValue);
        }
    }

    /** Replaces an element with a new value without doing any bounds-checking.

        This just sets a value directly in the array's internal storage, so you'd
        better make sure it's in range!

        @param indexToChange    the index whose value you want to change
        @param newValue         the new value to set for this index.
        @see set, getUnchecked
    */
    void setUnchecked (const int indexToChange, ParameterType newValue)
    {
        const ScopedLockType lock (getLock());
        bassert (isPositiveAndBelow (indexToChange, numUsed));
        data.elements [indexToChange] = newValue;
    }

    /** Adds elements from an array to the end of this array.

        @param elementsToAdd        the array of elements to add
        @param numElementsToAdd     how many elements are in this other array
        @see add
    */
    void addArray (const ElementType* elementsToAdd, int numElementsToAdd)
    {
        const ScopedLockType lock (getLock());

        if (numElementsToAdd > 0)
        {
            data.ensureAllocatedSize (numUsed + numElementsToAdd);

            while (--numElementsToAdd >= 0)
            {
                new (data.elements + numUsed) ElementType (*elementsToAdd++);
                ++numUsed;
            }
        }
    }

    /** This swaps the contents of this array with those of another array.

        If you need to exchange two arrays, this is vastly quicker than using copy-by-value
        because it just swaps their internal pointers.
    */
    template <class OtherArrayType>
    void swapWith (OtherArrayType& otherArray) noexcept
    {
        const ScopedLockType lock1 (getLock());
        const typename OtherArrayType::ScopedLockType lock2 (otherArray.getLock());

        data.swapWith (otherArray.data);
        std::swap (numUsed, otherArray.numUsed);
    }

    /** Adds elements from another array to the end of this array.

        @param arrayToAddFrom       the array from which to copy the elements
        @param startIndex           the first element of the other array to start copying from
        @param numElementsToAdd     how many elements to add from the other array. If this
                                    value is negative or greater than the number of available elements,
                                    all available elements will be copied.
        @see add
    */
    template <class OtherArrayType>
    void addArray (const OtherArrayType& arrayToAddFrom,
                   int startIndex = 0,
                   int numElementsToAdd = -1)
    {
        const typename OtherArrayType::ScopedLockType lock1 (arrayToAddFrom.getLock());

        {
            const ScopedLockType lock2 (getLock());

            if (startIndex < 0)
            {
                bassertfalse;
                startIndex = 0;
            }

            if (numElementsToAdd < 0 || startIndex + numElementsToAdd > arrayToAddFrom.size())
                numElementsToAdd = arrayToAddFrom.size() - startIndex;

            while (--numElementsToAdd >= 0)
                add (arrayToAddFrom.getUnchecked (startIndex++));
        }
    }

    /** This will enlarge or shrink the array to the given number of elements, by adding
        or removing items from its end.

        If the array is smaller than the given target size, empty elements will be appended
        until its size is as specified. If its size is larger than the target, items will be
        removed from its end to shorten it.
    */
    void resize (const int targetNumItems)
    {
        bassert (targetNumItems >= 0);

        const int numToAdd = targetNumItems - numUsed;
        if (numToAdd > 0)
            insertMultiple (numUsed, ElementType(), numToAdd);
        else if (numToAdd < 0)
            removeRange (targetNumItems, -numToAdd);
    }

    /** Inserts a new element into the array, assuming that the array is sorted.

        This will use a comparator to find the position at which the new element
        should go. If the array isn't sorted, the behaviour of this
        method will be unpredictable.

        @param comparator   the comparator to use to compare the elements - see the sort()
                            method for details about the form this object should take
        @param newElement   the new element to insert to the array
        @returns the index at which the new item was added
        @see addUsingDefaultSort, add, sort
    */
    template <class ElementComparator>
    int addSorted (ElementComparator& comparator, ParameterType newElement)
    {
        const ScopedLockType lock (getLock());
        const int index = findInsertIndexInSortedArray (comparator, data.elements.getData(), newElement, 0, numUsed);
        insert (index, newElement);
        return index;
    }

    /** Inserts a new element into the array, assuming that the array is sorted.

        This will use the DefaultElementComparator class for sorting, so your ElementType
        must be suitable for use with that class. If the array isn't sorted, the behaviour of this
        method will be unpredictable.

        @param newElement   the new element to insert to the array
        @see addSorted, sort
    */
    void addUsingDefaultSort (ParameterType newElement)
    {
        DefaultElementComparator <ElementType> comparator;
        addSorted (comparator, newElement);
    }

    /** Finds the index of an element in the array, assuming that the array is sorted.

        This will use a comparator to do a binary-chop to find the index of the given
        element, if it exists. If the array isn't sorted, the behaviour of this
        method will be unpredictable.

        @param comparator           the comparator to use to compare the elements - see the sort()
                                    method for details about the form this object should take
        @param elementToLookFor     the element to search for
        @returns                    the index of the element, or -1 if it's not found
        @see addSorted, sort
    */
    template <typename ElementComparator, typename TargetValueType>
    int indexOfSorted (ElementComparator& comparator, TargetValueType elementToLookFor) const
    {
        (void) comparator;  // if you pass in an object with a static compareElements() method, this
                            // avoids getting warning messages about the parameter being unused

        const ScopedLockType lock (getLock());

        for (int s = 0, e = numUsed;;)
        {
            if (s >= e)
                return -1;

            if (comparator.compareElements (elementToLookFor, data.elements [s]) == 0)
                return s;

            const int halfway = (s + e) / 2;
            if (halfway == s)
                return -1;

            if (comparator.compareElements (elementToLookFor, data.elements [halfway]) >= 0)
                s = halfway;
            else
                e = halfway;
        }
    }

    //==============================================================================
    /** Removes an element from the array.

        This will remove the element at a given index, and move back
        all the subsequent elements to close the gap.
        If the index passed in is out-of-range, nothing will happen.

        @param indexToRemove    the index of the element to remove
        @returns                the element that has been removed
        @see removeValue, removeRange
    */
    ElementType remove (const int indexToRemove)
    {
        const ScopedLockType lock (getLock());

        if (isPositiveAndBelow (indexToRemove, numUsed))
        {
            bassert (data.elements != nullptr); 
            ElementType removed (data.elements[indexToRemove]);
            removeInternal (indexToRemove);
            return removed;
        }

        return ElementType();
    }

    /** Removes an item from the array.

        This will remove the first occurrence of the given element from the array.
        If the item isn't found, no action is taken.

        @param valueToRemove   the object to try to remove
        @see remove, removeRange
    */
    void removeFirstMatchingValue (ParameterType valueToRemove)
    {
        const ScopedLockType lock (getLock());
        ElementType* const e = data.elements;

        for (int i = 0; i < numUsed; ++i)
        {
            if (valueToRemove == e[i])
            {
                removeInternal (i);
                break;
            }
        }
    }

    /** Removes an item from the array.

        This will remove the first occurrence of the given element from the array.
        If the item isn't found, no action is taken.

        @param valueToRemove   the object to try to remove
        @see remove, removeRange
    */
    void removeAllInstancesOf (ParameterType valueToRemove)
    {
        const ScopedLockType lock (getLock());

        for (int i = numUsed; --i >= 0;)
            if (valueToRemove == data.elements[i])
                removeInternal (i);
    }

    /** Removes a range of elements from the array.

        This will remove a set of elements, starting from the given index,
        and move subsequent elements down to close the gap.

        If the range extends beyond the bounds of the array, it will
        be safely clipped to the size of the array.

        @param startIndex       the index of the first element to remove
        @param numberToRemove   how many elements should be removed
        @see remove, removeFirstMatchingValue, removeAllInstancesOf
    */
    void removeRange (int startIndex, int numberToRemove)
    {
        const ScopedLockType lock (getLock());
        const int endIndex = blimit (0, numUsed, startIndex + numberToRemove);
        startIndex = blimit (0, numUsed, startIndex);

        if (endIndex > startIndex)
        {
            ElementType* const e = data.elements + startIndex;

            numberToRemove = endIndex - startIndex;
            for (int i = 0; i < numberToRemove; ++i)
                e[i].~ElementType();

            const int numToShift = numUsed - endIndex;
            if (numToShift > 0)
                memmove (e, e + numberToRemove, ((size_t) numToShift) * sizeof (ElementType));

            numUsed -= numberToRemove;
            minimiseStorageAfterRemoval();
        }
    }

    /** Removes the last n elements from the array.

        @param howManyToRemove   how many elements to remove from the end of the array
        @see remove, removeFirstMatchingValue, removeAllInstancesOf, removeRange
    */
    void removeLast (int howManyToRemove = 1)
    {
        const ScopedLockType lock (getLock());

        if (howManyToRemove > numUsed)
            howManyToRemove = numUsed;

        for (int i = 1; i <= howManyToRemove; ++i)
            data.elements [numUsed - i].~ElementType();

        numUsed -= howManyToRemove;
        minimiseStorageAfterRemoval();
    }

    /** Removes any elements which are also in another array.

        @param otherArray   the other array in which to look for elements to remove
        @see removeValuesNotIn, remove, removeFirstMatchingValue, removeAllInstancesOf, removeRange
    */
    template <class OtherArrayType>
    void removeValuesIn (const OtherArrayType& otherArray)
    {
        const typename OtherArrayType::ScopedLockType lock1 (otherArray.getLock());
        const ScopedLockType lock2 (getLock());

        if (this == &otherArray)
        {
            clear();
        }
        else
        {
            if (otherArray.size() > 0)
            {
                for (int i = numUsed; --i >= 0;)
                    if (otherArray.contains (data.elements [i]))
                        removeInternal (i);
            }
        }
    }

    /** Removes any elements which are not found in another array.

        Only elements which occur in this other array will be retained.

        @param otherArray    the array in which to look for elements NOT to remove
        @see removeValuesIn, remove, removeFirstMatchingValue, removeAllInstancesOf, removeRange
    */
    template <class OtherArrayType>
    void removeValuesNotIn (const OtherArrayType& otherArray)
    {
        const typename OtherArrayType::ScopedLockType lock1 (otherArray.getLock());
        const ScopedLockType lock2 (getLock());

        if (this != &otherArray)
        {
            if (otherArray.size() <= 0)
            {
                clear();
            }
            else
            {
                for (int i = numUsed; --i >= 0;)
                    if (! otherArray.contains (data.elements [i]))
                        removeInternal (i);
            }
        }
    }

    /** Swaps over two elements in the array.

        This swaps over the elements found at the two indexes passed in.
        If either index is out-of-range, this method will do nothing.

        @param index1   index of one of the elements to swap
        @param index2   index of the other element to swap
    */
    void swap (const int index1,
               const int index2)
    {
        const ScopedLockType lock (getLock());

        if (isPositiveAndBelow (index1, numUsed)
             && isPositiveAndBelow (index2, numUsed))
        {
            std::swap (data.elements [index1],
                       data.elements [index2]);
        }
    }

    /** Moves one of the values to a different position.

        This will move the value to a specified index, shuffling along
        any intervening elements as required.

        So for example, if you have the array { 0, 1, 2, 3, 4, 5 } then calling
        move (2, 4) would result in { 0, 1, 3, 4, 2, 5 }.

        @param currentIndex     the index of the value to be moved. If this isn't a
                                valid index, then nothing will be done
        @param newIndex         the index at which you'd like this value to end up. If this
                                is less than zero, the value will be moved to the end
                                of the array
    */
    void move (const int currentIndex, int newIndex) noexcept
    {
        if (currentIndex != newIndex)
        {
            const ScopedLockType lock (getLock());

            if (isPositiveAndBelow (currentIndex, numUsed))
            {
                if (! isPositiveAndBelow (newIndex, numUsed))
                    newIndex = numUsed - 1;

                char tempCopy [sizeof (ElementType)];
                memcpy (tempCopy, data.elements + currentIndex, sizeof (ElementType));

                if (newIndex > currentIndex)
                {
                    memmove (data.elements + currentIndex,
                             data.elements + currentIndex + 1,
                             sizeof (ElementType) * (size_t) (newIndex - currentIndex));
                }
                else
                {
                    memmove (data.elements + newIndex + 1,
                             data.elements + newIndex,
                             sizeof (ElementType) * (size_t) (currentIndex - newIndex));
                }

                memcpy (data.elements + newIndex, tempCopy, sizeof (ElementType));
            }
        }
    }

    //==============================================================================
    /** Reduces the amount of storage being used by the array.

        Arrays typically allocate slightly more storage than they need, and after
        removing elements, they may have quite a lot of unused space allocated.
        This method will reduce the amount of allocated storage to a minimum.
    */
    void minimiseStorageOverheads()
    {
        const ScopedLockType lock (getLock());
        data.shrinkToNoMoreThan (numUsed);
    }

    /** Increases the array's internal storage to hold a minimum number of elements.

        Calling this before adding a large known number of elements means that
        the array won't have to keep dynamically resizing itself as the elements
        are added, and it'll therefore be more efficient.
    */
    void ensureStorageAllocated (const int minNumElements)
    {
        const ScopedLockType lock (getLock());
        data.ensureAllocatedSize (minNumElements);
    }

    //==============================================================================
    /** Sorts the elements in the array.

        This will use a comparator object to sort the elements into order. The object
        passed must have a method of the form:
        @code
        int compareElements (ElementType first, ElementType second);
        @endcode

        ..and this method must return:
          - a value of < 0 if the first comes before the second
          - a value of 0 if the two objects are equivalent
          - a value of > 0 if the second comes before the first

        To improve performance, the compareElements() method can be declared as static or const.

        @param comparator   the comparator to use for comparing elements.
        @param retainOrderOfEquivalentItems     if this is true, then items
                            which the comparator says are equivalent will be
                            kept in the order in which they currently appear
                            in the array. This is slower to perform, but may
                            be important in some cases. If it's false, a faster
                            algorithm is used, but equivalent elements may be
                            rearranged.

        @see addSorted, indexOfSorted, sortArray
    */
    template <class ElementComparator>
    void sort (ElementComparator& comparator,
               const bool retainOrderOfEquivalentItems = false) const
    {
        const ScopedLockType lock (getLock());
        (void) comparator;  // if you pass in an object with a static compareElements() method, this
                            // avoids getting warning messages about the parameter being unused
        sortArray (comparator, data.elements.getData(), 0, size() - 1, retainOrderOfEquivalentItems);
    }

    //==============================================================================
    /** Returns the CriticalSection that locks this array.
        To lock, you can call getLock().enter() and getLock().exit(), or preferably use
        an object of ScopedLockType as an RAII lock for it.
    */
    inline const TypeOfCriticalSectionToUse& getLock() const noexcept      { return data; }

    /** Returns the type of scoped lock to use for locking this array */
    typedef typename TypeOfCriticalSectionToUse::ScopedLockType ScopedLockType;


private:
    //==============================================================================
    ArrayAllocationBase <ElementType, TypeOfCriticalSectionToUse> data;
    int numUsed;

    void removeInternal (const int indexToRemove)
    {
        --numUsed;
        ElementType* const e = data.elements + indexToRemove;
        e->~ElementType();
        const int numberToShift = numUsed - indexToRemove;

        if (numberToShift > 0)
            memmove (e, e + 1, ((size_t) numberToShift) * sizeof (ElementType));

        minimiseStorageAfterRemoval();
    }

    inline void deleteAllElements() noexcept
    {
        for (int i = 0; i < numUsed; ++i)
            data.elements[i].~ElementType();
    }

    void minimiseStorageAfterRemoval()
    {
        if (data.numAllocated > bmax (minimumAllocatedSize, numUsed * 2))
            data.shrinkToNoMoreThan (bmax (numUsed, bmax (minimumAllocatedSize, 64 / (int) sizeof (ElementType))));
    }
};

#endif   // BEAST_ARRAY_H_INCLUDED
