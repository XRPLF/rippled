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

namespace beast
{

std::int64_t InputStream::getNumBytesRemaining()
{
    std::int64_t len = getTotalLength();

    if (len >= 0)
        len -= getPosition();

    return len;
}

char InputStream::readByte()
{
    char temp = 0;
    read (&temp, 1);
    return temp;
}

bool InputStream::readBool()
{
    return readByte() != 0;
}

short InputStream::readShort()
{
    char temp[2];

    if (read (temp, 2) == 2)
        return (short) ByteOrder::littleEndianShort (temp);

    return 0;
}

short InputStream::readShortBigEndian()
{
    char temp[2];

    if (read (temp, 2) == 2)
        return (short) ByteOrder::bigEndianShort (temp);

    return 0;
}

int InputStream::readInt()
{
    static_bassert (sizeof (int) == 4);

    char temp[4];

    if (read (temp, 4) == 4)
        return (int) ByteOrder::littleEndianInt (temp);

    return 0;
}

std::int32_t InputStream::readInt32()
{
    char temp[4];

    if (read (temp, 4) == 4)
        return (std::int32_t) ByteOrder::littleEndianInt (temp);

    return 0;
}

int InputStream::readIntBigEndian()
{
    char temp[4];

    if (read (temp, 4) == 4)
        return (int) ByteOrder::bigEndianInt (temp);

    return 0;
}

std::int32_t InputStream::readInt32BigEndian()
{
    char temp[4];

    if (read (temp, 4) == 4)
        return (std::int32_t) ByteOrder::bigEndianInt (temp);

    return 0;
}

int InputStream::readCompressedInt()
{
    const std::uint8_t sizeByte = (std::uint8_t) readByte();
    if (sizeByte == 0)
        return 0;

    const int numBytes = (sizeByte & 0x7f);
    if (numBytes > 4)
    {
        bassertfalse;    // trying to read corrupt data - this method must only be used
                       // to read data that was written by OutputStream::writeCompressedInt()
        return 0;
    }

    char bytes[4] = { 0, 0, 0, 0 };
    if (read (bytes, numBytes) != numBytes)
        return 0;

    const int num = (int) ByteOrder::littleEndianInt (bytes);
    return (sizeByte >> 7) ? -num : num;
}

std::int64_t InputStream::readInt64()
{
    union { std::uint8_t asBytes[8]; std::uint64_t asInt64; } n;

    if (read (n.asBytes, 8) == 8)
        return (std::int64_t) ByteOrder::swapIfBigEndian (n.asInt64);

    return 0;
}

std::int64_t InputStream::readInt64BigEndian()
{
    union { std::uint8_t asBytes[8]; std::uint64_t asInt64; } n;

    if (read (n.asBytes, 8) == 8)
        return (std::int64_t) ByteOrder::swapIfLittleEndian (n.asInt64);

    return 0;
}

float InputStream::readFloat()
{
    // the union below relies on these types being the same size...
    static_bassert (sizeof (std::int32_t) == sizeof (float));
    union { std::int32_t asInt; float asFloat; } n;
    n.asInt = (std::int32_t) readInt();
    return n.asFloat;
}

float InputStream::readFloatBigEndian()
{
    union { std::int32_t asInt; float asFloat; } n;
    n.asInt = (std::int32_t) readIntBigEndian();
    return n.asFloat;
}

double InputStream::readDouble()
{
    union { std::int64_t asInt; double asDouble; } n;
    n.asInt = readInt64();
    return n.asDouble;
}

double InputStream::readDoubleBigEndian()
{
    union { std::int64_t asInt; double asDouble; } n;
    n.asInt = readInt64BigEndian();
    return n.asDouble;
}

String InputStream::readString()
{
    MemoryBlock buffer (256);
    char* data = static_cast<char*> (buffer.getData());
    size_t i = 0;

    while ((data[i] = readByte()) != 0)
    {
        if (++i >= buffer.getSize())
        {
            buffer.setSize (buffer.getSize() + 512);
            data = static_cast<char*> (buffer.getData());
        }
    }

    return String (CharPointer_UTF8 (data),
                   CharPointer_UTF8 (data + i));
}

String InputStream::readNextLine()
{
    MemoryBlock buffer (256);
    char* data = static_cast<char*> (buffer.getData());
    size_t i = 0;

    while ((data[i] = readByte()) != 0)
    {
        if (data[i] == '\n')
            break;

        if (data[i] == '\r')
        {
            const std::int64_t lastPos = getPosition();

            if (readByte() != '\n')
                setPosition (lastPos);

            break;
        }

        if (++i >= buffer.getSize())
        {
            buffer.setSize (buffer.getSize() + 512);
            data = static_cast<char*> (buffer.getData());
        }
    }

    return String::fromUTF8 (data, (int) i);
}

int InputStream::readIntoMemoryBlock (MemoryBlock& block, std::ptrdiff_t numBytes)
{
    MemoryOutputStream mo (block, true);
    return mo.writeFromInputStream (*this, numBytes);
}

String InputStream::readEntireStreamAsString()
{
    MemoryOutputStream mo;
    mo << *this;
    return mo.toString();
}

//==============================================================================
void InputStream::skipNextBytes (std::int64_t numBytesToSkip)
{
    if (numBytesToSkip > 0)
    {
        const int skipBufferSize = (int) bmin (numBytesToSkip, (std::int64_t) 16384);
        HeapBlock<char> temp ((size_t) skipBufferSize);

        while (numBytesToSkip > 0 && ! isExhausted())
            numBytesToSkip -= read (temp, (int) bmin (numBytesToSkip, (std::int64_t) skipBufferSize));
    }
}

//------------------------------------------------------------------------------

// Unfortunately, putting these in the header causes duplicate
// definition linker errors, even with the inline keyword!

template <>
char InputStream::readType <char> () { return readByte (); }

template <>
short InputStream::readType <short> () { return readShort (); }

template <>
std::int32_t InputStream::readType <std::int32_t> () { return readInt32 (); }

template <>
std::int64_t InputStream::readType <std::int64_t> () { return readInt64 (); }

template <>
unsigned char InputStream::readType <unsigned char> () { return static_cast <unsigned char> (readByte ()); }

template <>
unsigned short InputStream::readType <unsigned short> () { return static_cast <unsigned short> (readShort ()); }

template <>
std::uint32_t InputStream::readType <std::uint32_t> () { return static_cast <std::uint32_t> (readInt32 ()); }

template <>
std::uint64_t InputStream::readType <std::uint64_t> () { return static_cast <std::uint64_t> (readInt64 ()); }

template <>
float InputStream::readType <float> () { return readFloat (); }

template <>
double InputStream::readType <double> () { return readDouble (); }

//------------------------------------------------------------------------------

template <>
char InputStream::readTypeBigEndian <char> () { return readByte (); }

template <>
short InputStream::readTypeBigEndian <short> () { return readShortBigEndian (); }

template <>
std::int32_t InputStream::readTypeBigEndian <std::int32_t> () { return readInt32BigEndian (); }

template <>
std::int64_t InputStream::readTypeBigEndian <std::int64_t> () { return readInt64BigEndian (); }

template <>
unsigned char InputStream::readTypeBigEndian <unsigned char> () { return static_cast <unsigned char> (readByte ()); }

template <>
unsigned short InputStream::readTypeBigEndian <unsigned short> () { return static_cast <unsigned short> (readShortBigEndian ()); }

template <>
std::uint32_t InputStream::readTypeBigEndian <std::uint32_t> () { return static_cast <std::uint32_t> (readInt32BigEndian ()); }

template <>
std::uint64_t InputStream::readTypeBigEndian <std::uint64_t> () { return static_cast <std::uint64_t> (readInt64BigEndian ()); }

template <>
float InputStream::readTypeBigEndian <float> () { return readFloatBigEndian (); }

template <>
double InputStream::readTypeBigEndian <double> () { return readDoubleBigEndian (); }

} // beast
