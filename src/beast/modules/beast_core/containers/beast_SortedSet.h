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

#ifndef BEAST_SORTEDSET_H_INCLUDED
#define BEAST_SORTEDSET_H_INCLUDED

#if BEAST_MSVC
  #pragma warning (push)
  #pragma warning (disable: 4512)
#endif

//==============================================================================
/**
    Holds a set of unique primitive objects, such as ints or doubles.

    A set can only hold one item with a given value, so if for example it's a
    set of integers, attempting to add the same integer twice will do nothing
    the second time.

    Internally, the list of items is kept sorted (which means that whatever
    kind of primitive type is used must support the ==, <, >, <= and >= operators
    to determine the order), and searching the set for known values is very fast
    because it uses a binary-chop method.

    Note that if you're using a class or struct as the element type, it must be
    capable of being copied or moved with a straightforward memcpy, rather than
    needing construction and destruction code.

    To make all the set's methods thread-safe, pass in "CriticalSection" as the templated
    TypeOfCriticalSectionToUse parameter, instead of the default DummyCriticalSection.

    @see Array, OwnedArray, SharedObjectArray, StringArray, CriticalSection
*/
template <class ElementType, class TypeOfCriticalSectionToUse = DummyCriticalSection>
class SortedSet
{
public:
    //==============================================================================
    /** Creates an empty set. */
    SortedSet() noexcept
    {
    }

    /** Creates a copy of another set.
        @param other    the set to copy
    */
    SortedSet (const SortedSet& other)
        : data (other.data)
    {
    }

    /** Destructor. */
    ~SortedSet() noexcept
    {
    }

    /** Copies another set over this one.
        @param other    the set to copy
    */
    SortedSet& operator= (const SortedSet& other) noexcept
    {
        data = other.data;
        return *this;
    }

    //==============================================================================
    /** Compares this set to another one.
        Two sets are considered equal if they both contain the same set of elements.
        @param other    the other set to compare with
    */
    bool operator== (const SortedSet<ElementType>& other) const noexcept
    {
        return data == other.data;
    }

    /** Compares this set to another one.
        Two sets are considered equal if they both contain the same set of elements.
        @param other    the other set to compare with
    */
    bool operator!= (const SortedSet<ElementType>& other) const noexcept
    {
        return ! operator== (other);
    }

    //==============================================================================
    /** Removes all elements from the set.

        This will remove all the elements, and free any storage that the set is
        using. To clear it without freeing the storage, use the clearQuick()
        method instead.

        @see clearQuick
    */
    void clear() noexcept
    {
        data.clear();
    }

    /** Removes all elements from the set without freeing the array's allocated storage.

        @see clear
    */
    void clearQuick() noexcept
    {
        data.clearQuick();
    }

    //==============================================================================
    /** Returns the current number of elements in the set.
    */
    inline int size() const noexcept
    {
        return data.size();
    }

    /** Returns one of the elements in the set.

        If the index passed in is beyond the range of valid elements, this
        will return zero.

        If you're certain that the index will always be a valid element, you
        can call getUnchecked() instead, which is faster.

        @param index    the index of the element being requested (0 is the first element in the set)
        @see getUnchecked, getFirst, getLast
    */
    inline ElementType operator[] (const int index) const noexcept
    {
        return data [index];
    }

    /** Returns one of the elements in the set, without checking the index passed in.
        Unlike the operator[] method, this will try to return an element without
        checking that the index is within the bounds of the set, so should only
        be used when you're confident that it will always be a valid index.

        @param index    the index of the element being requested (0 is the first element in the set)
        @see operator[], getFirst, getLast
    */
    inline ElementType getUnchecked (const int index) const noexcept
    {
        return data.getUnchecked (index);
    }

    /** Returns a direct reference to one of the elements in the set, without checking the index passed in.

        This is like getUnchecked, but returns a direct reference to the element, so that
        you can alter it directly. Obviously this can be dangerous, so only use it when
        absolutely necessary.

        @param index    the index of the element being requested (0 is the first element in the array)
    */
    inline ElementType& getReference (const int index) const noexcept
    {
        return data.getReference (index);
    }

    /** Returns the first element in the set, or 0 if the set is empty.

        @see operator[], getUnchecked, getLast
    */
    inline ElementType getFirst() const noexcept
    {
        return data.getFirst();
    }

    /** Returns the last element in the set, or 0 if the set is empty.

        @see operator[], getUnchecked, getFirst
    */
    inline ElementType getLast() const noexcept
    {
        return data.getLast();
    }

    //==============================================================================
    /** Returns a pointer to the first element in the set.
        This method is provided for compatibility with standard C++ iteration mechanisms.
    */
    inline ElementType* begin() const noexcept
    {
        return data.begin();
    }

    /** Returns a pointer to the element which follows the last element in the set.
        This method is provided for compatibility with standard C++ iteration mechanisms.
    */
    inline ElementType* end() const noexcept
    {
        return data.end();
    }

    //==============================================================================
    /** Finds the index of the first element which matches the value passed in.

        This will search the set for the given object, and return the index
        of its first occurrence. If the object isn't found, the method will return -1.

        @param elementToLookFor   the value or object to look for
        @returns                  the index of the object, or -1 if it's not found
    */
    int indexOf (const ElementType& elementToLookFor) const noexcept
    {
        const ScopedLockType lock (data.getLock());

        int s = 0;
        int e = data.size();

        for (;;)
        {
            if (s >= e)
                return -1;

            if (elementToLookFor == data.getReference (s))
                return s;

            const int halfway = (s + e) / 2;

            if (halfway == s)
                return -1;
            else if (elementToLookFor < data.getReference (halfway))
                e = halfway;
            else
                s = halfway;
        }
    }

    /** Returns true if the set contains at least one occurrence of an object.

        @param elementToLookFor     the value or object to look for
        @returns                    true if the item is found
    */
    bool contains (const ElementType& elementToLookFor) const noexcept
    {
        return indexOf (elementToLookFor) >= 0;
    }

    //==============================================================================
    /** Adds a new element to the set, (as long as it's not already in there).

        Note that if a matching element already exists, the new value will be assigned
        to the existing one using operator=, so that if there are any differences between
        the objects which were not recognised by the object's operator==, then the
        set will always contain a copy of the most recently added one.

        @param newElement   the new object to add to the set
        @returns            true if the value was added, or false if it already existed
        @see set, insert, addIfNotAlreadyThere, addSorted, addSet, addArray
    */
    bool add (const ElementType& newElement) noexcept
    {
        const ScopedLockType lock (getLock());

        int s = 0;
        int e = data.size();

        while (s < e)
        {
            ElementType& elem = data.getReference (s);
            if (newElement == elem)
            {
                elem = newElement; // force an update in case operator== permits differences.
                return false;
            }

            const int halfway = (s + e) / 2;
            const bool isBeforeHalfway = (newElement < data.getReference (halfway));

            if (halfway == s)
            {
                if (! isBeforeHalfway)
                    ++s;

                break;
            }
            else if (isBeforeHalfway)
                e = halfway;
            else
                s = halfway;
        }

        data.insert (s, newElement);
        return true;
    }

    /** Adds elements from an array to this set.

        @param elementsToAdd        the array of elements to add
        @param numElementsToAdd     how many elements are in this other array
        @see add
    */
    void addArray (const ElementType* elementsToAdd,
                   int numElementsToAdd) noexcept
    {
        const ScopedLockType lock (getLock());

        while (--numElementsToAdd >= 0)
            add (*elementsToAdd++);
    }

    /** Adds elements from another set to this one.

        @param setToAddFrom         the set from which to copy the elements
        @param startIndex           the first element of the other set to start copying from
        @param numElementsToAdd     how many elements to add from the other set. If this
                                    value is negative or greater than the number of available elements,
                                    all available elements will be copied.
        @see add
    */
    template <class OtherSetType>
    void addSet (const OtherSetType& setToAddFrom,
                 int startIndex = 0,
                 int numElementsToAdd = -1) noexcept
    {
        const typename OtherSetType::ScopedLockType lock1 (setToAddFrom.getLock());

        {
            const ScopedLockType lock2 (getLock());
            bassert (this != &setToAddFrom);

            if (this != &setToAddFrom)
            {
                if (startIndex < 0)
                {
                    bassertfalse;
                    startIndex = 0;
                }

                if (numElementsToAdd < 0 || startIndex + numElementsToAdd > setToAddFrom.size())
                    numElementsToAdd = setToAddFrom.size() - startIndex;

                if (numElementsToAdd > 0)
                    addArray (&setToAddFrom.data.getReference (startIndex), numElementsToAdd);
            }
        }
    }

    //==============================================================================
    /** Removes an element from the set.

        This will remove the element at a given index.
        If the index passed in is out-of-range, nothing will happen.

        @param indexToRemove    the index of the element to remove
        @returns                the element that has been removed
        @see removeValue, removeRange
    */
    ElementType remove (const int indexToRemove) noexcept
    {
        return data.remove (indexToRemove);
    }

    /** Removes an item from the set.

        This will remove the given element from the set, if it's there.

        @param valueToRemove   the object to try to remove
        @see remove, removeRange
    */
    void removeValue (const ElementType valueToRemove) noexcept
    {
        const ScopedLockType lock (getLock());
        data.remove (indexOf (valueToRemove));
    }

    /** Removes any elements which are also in another set.

        @param otherSet   the other set in which to look for elements to remove
        @see removeValuesNotIn, remove, removeValue, removeRange
    */
    template <class OtherSetType>
    void removeValuesIn (const OtherSetType& otherSet) noexcept
    {
        const typename OtherSetType::ScopedLockType lock1 (otherSet.getLock());
        const ScopedLockType lock2 (getLock());

        if (this == &otherSet)
        {
            clear();
        }
        else if (otherSet.size() > 0)
        {
            for (int i = data.size(); --i >= 0;)
                if (otherSet.contains (data.getReference (i)))
                    remove (i);
        }
    }

    /** Removes any elements which are not found in another set.

        Only elements which occur in this other set will be retained.

        @param otherSet    the set in which to look for elements NOT to remove
        @see removeValuesIn, remove, removeValue, removeRange
    */
    template <class OtherSetType>
    void removeValuesNotIn (const OtherSetType& otherSet) noexcept
    {
        const typename OtherSetType::ScopedLockType lock1 (otherSet.getLock());
        const ScopedLockType lock2 (getLock());

        if (this != &otherSet)
        {
            if (otherSet.size() <= 0)
            {
                clear();
            }
            else
            {
                for (int i = data.size(); --i >= 0;)
                    if (! otherSet.contains (data.getReference (i)))
                        remove (i);
            }
        }
    }

    /** This swaps the contents of this array with those of another array.

        If you need to exchange two arrays, this is vastly quicker than using copy-by-value
        because it just swaps their internal pointers.
    */
    template <class OtherSortedSetType>
    void swapWith (OtherSortedSetType& otherSet) noexcept
    {
        data.swapWith (otherSet.data);
    }

    //==============================================================================
    /** Reduces the amount of storage being used by the set.

        Sets typically allocate slightly more storage than they need, and after
        removing elements, they may have quite a lot of unused space allocated.
        This method will reduce the amount of allocated storage to a minimum.
    */
    void minimiseStorageOverheads() noexcept
    {
        data.minimiseStorageOverheads();
    }

    /** Increases the set's internal storage to hold a minimum number of elements.

        Calling this before adding a large known number of elements means that
        the set won't have to keep dynamically resizing itself as the elements
        are added, and it'll therefore be more efficient.
    */
    void ensureStorageAllocated (const int minNumElements)
    {
        data.ensureStorageAllocated (minNumElements);
    }

    //==============================================================================
    /** Returns the CriticalSection that locks this array.
        To lock, you can call getLock().enter() and getLock().exit(), or preferably use
        an object of ScopedLockType as an RAII lock for it.
    */
    inline const TypeOfCriticalSectionToUse& getLock() const noexcept      { return data.getLock(); }

    /** Returns the type of scoped lock to use for locking this array */
    typedef typename TypeOfCriticalSectionToUse::ScopedLockType ScopedLockType;


private:
    //==============================================================================
    Array <ElementType, TypeOfCriticalSectionToUse> data;
};

#if BEAST_MSVC
  #pragma warning (pop)
#endif

#endif

