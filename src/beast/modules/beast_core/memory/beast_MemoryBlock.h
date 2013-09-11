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

#ifndef BEAST_MEMORYBLOCK_H_INCLUDED
#define BEAST_MEMORYBLOCK_H_INCLUDED

//==============================================================================
/**
    A class to hold a resizable block of raw data.

*/
class BEAST_API MemoryBlock : LeakChecked <MemoryBlock>
{
public:
    //==============================================================================
    /** Create an uninitialised block with 0 size. */
    MemoryBlock() noexcept;

    /** Creates a memory block with a given initial size.

        @param initialSize          the size of block to create
        @param initialiseToZero     whether to clear the memory or just leave it uninitialised
    */
    MemoryBlock (const size_t initialSize,
                 bool initialiseToZero = false);

    /** Creates a copy of another memory block. */
    MemoryBlock (const MemoryBlock& other);

    /** Creates a memory block using a copy of a block of data.

        @param dataToInitialiseFrom     some data to copy into this block
        @param sizeInBytes              how much space to use
    */
    MemoryBlock (const void* dataToInitialiseFrom, size_t sizeInBytes);

    /** Destructor. */
    ~MemoryBlock() noexcept;

    /** Copies another memory block onto this one.

        This block will be resized and copied to exactly match the other one.
    */
    MemoryBlock& operator= (const MemoryBlock& other);

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    MemoryBlock (MemoryBlock&& other) noexcept;
    MemoryBlock& operator= (MemoryBlock&& other) noexcept;
   #endif

    // Standard container interface
    typedef char* iterator;
    typedef char const* const_iterator;
    inline iterator begin () noexcept { return static_cast <iterator> (getData ()); }
    inline iterator end () noexcept { return addBytesToPointer (begin (), size); }
    inline const_iterator cbegin () const noexcept { return static_cast <const_iterator> (getConstData ()); }
    inline const_iterator cend () const noexcept { return addBytesToPointer (cbegin (), size); }

    //==============================================================================
    /** Compares two memory blocks.

        @returns true only if the two blocks are the same size and have identical contents.
    */
    bool operator== (const MemoryBlock& other) const noexcept;

    /** Compares two memory blocks.

        @returns true if the two blocks are different sizes or have different contents.
    */
    bool operator!= (const MemoryBlock& other) const noexcept;

    /** Returns true if the data in this MemoryBlock matches the raw bytes passed-in.
    */
    bool matches (const void* data, size_t dataSize) const noexcept;

    //==============================================================================
    /** Returns a void pointer to the data.

        Note that the pointer returned will probably become invalid when the
        block is resized.
    */
    void* getData() const noexcept
    {
        return data;
    }

    /** Returns a void pointer to data as unmodifiable.

        Note that the pointer returned will probably become invalid when the
        block is resized.
    */
    void const* getConstData() const noexcept
    {
        return data;
    }

    /** Returns a byte from the memory block.

        This returns a reference, so you can also use it to set a byte.
    */
    template <typename Type>
    char& operator[] (const Type offset) const noexcept             { return data [offset]; }

    //==============================================================================
    /** Returns the block's current allocated size, in bytes. */
    size_t getSize() const noexcept                                 { return size; }

    /** Resizes the memory block.

        This will try to keep as much of the block's current content as it can,
        and can optionally be made to clear any new space that gets allocated at
        the end of the block.

        @param newSize                      the new desired size for the block
        @param initialiseNewSpaceToZero     if the block gets enlarged, this determines
                                            whether to clear the new section or just leave it
                                            uninitialised
        @see ensureSize
    */
    void setSize (const size_t newSize,
                  bool initialiseNewSpaceToZero = false);

    /** Increases the block's size only if it's smaller than a given size.

        @param minimumSize                  if the block is already bigger than this size, no action
                                            will be taken; otherwise it will be increased to this size
        @param initialiseNewSpaceToZero     if the block gets enlarged, this determines
                                            whether to clear the new section or just leave it
                                            uninitialised
        @see setSize
    */
    void ensureSize (const size_t minimumSize,
                     bool initialiseNewSpaceToZero = false);

    //==============================================================================
    /** Fills the entire memory block with a repeated byte value.

        This is handy for clearing a block of memory to zero.
    */
    void fillWith (uint8 valueToUse) noexcept;

    /** Adds another block of data to the end of this one.
        The data pointer must not be null. This block's size will be increased accordingly.
    */
    void append (const void* data, size_t numBytes);

    /** Resizes this block to the given size and fills its contents from the supplied buffer.
        The data pointer must not be null.
    */
    void replaceWith (const void* data, size_t numBytes);

    /** Inserts some data into the block.
        The dataToInsert pointer must not be null. This block's size will be increased accordingly.
        If the insert position lies outside the valid range of the block, it will be clipped to
        within the range before being used.
    */
    void insert (const void* dataToInsert, size_t numBytesToInsert, size_t insertPosition);

    /** Chops out a section  of the block.

        This will remove a section of the memory block and close the gap around it,
        shifting any subsequent data downwards and reducing the size of the block.

        If the range specified goes beyond the size of the block, it will be clipped.
    */
    void removeSection (size_t startByte, size_t numBytesToRemove);

    //==============================================================================
    /** Copies data into this MemoryBlock from a memory address.

        @param srcData              the memory location of the data to copy into this block
        @param destinationOffset    the offset in this block at which the data being copied should begin
        @param numBytes             how much to copy in (if this goes beyond the size of the memory block,
                                    it will be clipped so not to do anything nasty)
    */
    void copyFrom (const void* srcData,
                   int destinationOffset,
                   size_t numBytes) noexcept;

    /** Copies data from this MemoryBlock to a memory address.

        @param destData         the memory location to write to
        @param sourceOffset     the offset within this block from which the copied data will be read
        @param numBytes         how much to copy (if this extends beyond the limits of the memory block,
                                zeros will be used for that portion of the data)
    */
    void copyTo (void* destData,
                 int sourceOffset,
                 size_t numBytes) const noexcept;

    //==============================================================================
    /** Exchanges the contents of this and another memory block.
        No actual copying is required for this, so it's very fast.
    */
    void swapWith (MemoryBlock& other) noexcept;

    //==============================================================================
    /** Attempts to parse the contents of the block as a zero-terminated UTF8 string. */
    String toString() const;

    //==============================================================================
    /** Parses a string of hexadecimal numbers and writes this data into the memory block.

        The block will be resized to the number of valid bytes read from the string.
        Non-hex characters in the string will be ignored.

        @see String::toHexString()
    */
    void loadFromHexString (const String& sourceHexString);

    //==============================================================================
    /** Sets a number of bits in the memory block, treating it as a long binary sequence. */
    void setBitRange (size_t bitRangeStart,
                      size_t numBits,
                      int binaryNumberToApply) noexcept;

    /** Reads a number of bits from the memory block, treating it as one long binary sequence */
    int getBitRange (size_t bitRangeStart,
                     size_t numBitsToRead) const noexcept;

    //==============================================================================
    /** Returns a string of characters that represent the binary contents of this block.

        Uses a 64-bit encoding system to allow binary data to be turned into a string
        of simple non-extended characters, e.g. for storage in XML.

        @see fromBase64Encoding
    */
    String toBase64Encoding() const;

    /** Takes a string of encoded characters and turns it into binary data.

        The string passed in must have been created by to64BitEncoding(), and this
        block will be resized to recreate the original data block.

        @see toBase64Encoding
    */
    bool fromBase64Encoding  (const String& encodedString);


private:
    //==============================================================================
    HeapBlock <char> data;
    size_t size;
};

#endif

