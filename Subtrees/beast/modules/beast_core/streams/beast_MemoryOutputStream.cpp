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

MemoryOutputStream::MemoryOutputStream (const size_t initialSize)
  : data (internalBlock),
    position (0),
    size (0)
{
    internalBlock.setSize (initialSize, false);
}

MemoryOutputStream::MemoryOutputStream (MemoryBlock& memoryBlockToWriteTo,
                                        const bool appendToExistingBlockContent)
  : data (memoryBlockToWriteTo),
    position (0),
    size (0)
{
    if (appendToExistingBlockContent)
        position = size = memoryBlockToWriteTo.getSize();
}

MemoryOutputStream::~MemoryOutputStream()
{
    trimExternalBlockSize();
}

void MemoryOutputStream::flush()
{
    trimExternalBlockSize();
}

void MemoryOutputStream::trimExternalBlockSize()
{
    if (&data != &internalBlock)
        data.setSize (size, false);
}

void MemoryOutputStream::preallocate (const size_t bytesToPreallocate)
{
    data.ensureSize (bytesToPreallocate + 1);
}

void MemoryOutputStream::reset() noexcept
{
    position = 0;
    size = 0;
}

char* MemoryOutputStream::prepareToWrite (size_t numBytes)
{
    bassert ((ssize_t) numBytes >= 0);
    size_t storageNeeded = position + numBytes;

    if (storageNeeded >= data.getSize())
        data.ensureSize ((storageNeeded + bmin (storageNeeded / 2, (size_t) (1024 * 1024)) + 32) & ~31u);

    char* const writePointer = static_cast <char*> (data.getData()) + position;
    position += numBytes;
    size = bmax (size, position);
    return writePointer;
}

bool MemoryOutputStream::write (const void* const buffer, size_t howMany)
{
    bassert (buffer != nullptr && ((ssize_t) howMany) >= 0);

    if (howMany > 0)
        memcpy (prepareToWrite (howMany), buffer, howMany);

    return true;
}

void MemoryOutputStream::writeRepeatedByte (uint8 byte, size_t howMany)
{
    if (howMany > 0)
        memset (prepareToWrite (howMany), byte, howMany);
}

void MemoryOutputStream::appendUTF8Char (beast_wchar c)
{
    CharPointer_UTF8 (prepareToWrite (CharPointer_UTF8::getBytesRequiredFor (c))).write (c);
}

MemoryBlock MemoryOutputStream::getMemoryBlock() const
{
    return MemoryBlock (getData(), getDataSize());
}

const void* MemoryOutputStream::getData() const noexcept
{
    if (data.getSize() > size)
        static_cast <char*> (data.getData()) [size] = 0;

    return data.getData();
}

bool MemoryOutputStream::setPosition (int64 newPosition)
{
    if (newPosition <= (int64) size)
    {
        // ok to seek backwards
        position = blimit ((size_t) 0, size, (size_t) newPosition);
        return true;
    }

    // can't move beyond the end of the stream..
    return false;
}

int MemoryOutputStream::writeFromInputStream (InputStream& source, int64 maxNumBytesToWrite)
{
    // before writing from an input, see if we can preallocate to make it more efficient..
    int64 availableData = source.getTotalLength() - source.getPosition();

    if (availableData > 0)
    {
        if (maxNumBytesToWrite > availableData)
            maxNumBytesToWrite = availableData;

        preallocate (data.getSize() + (size_t) maxNumBytesToWrite);
    }

    return OutputStream::writeFromInputStream (source, maxNumBytesToWrite);
}

String MemoryOutputStream::toUTF8() const
{
    const char* const d = static_cast <const char*> (getData());
    return String (CharPointer_UTF8 (d), CharPointer_UTF8 (d + getDataSize()));
}

String MemoryOutputStream::toString() const
{
    return String::createStringFromData (getData(), (int) getDataSize());
}

OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const MemoryOutputStream& streamToRead)
{
    const size_t dataSize = streamToRead.getDataSize();

    if (dataSize > 0)
        stream.write (streamToRead.getData(), dataSize);

    return stream;
}
