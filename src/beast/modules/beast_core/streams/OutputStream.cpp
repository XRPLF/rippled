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

#if BEAST_DEBUG

struct DanglingStreamChecker
{
    DanglingStreamChecker() {}

    ~DanglingStreamChecker()
    {
        /*
            It's always a bad idea to leak any object, but if you're leaking output
            streams, then there's a good chance that you're failing to flush a file
            to disk properly, which could result in corrupted data and other similar
            nastiness..
        */
        bassert (activeStreams.size() == 0);
    }

    Array<void*, CriticalSection> activeStreams;
};

static DanglingStreamChecker danglingStreamChecker;
#endif

//==============================================================================
OutputStream::OutputStream()
    : newLineString (NewLine::getDefault())
{
   #if BEAST_DEBUG
    danglingStreamChecker.activeStreams.add (this);
   #endif
}

OutputStream::~OutputStream()
{
   #if BEAST_DEBUG
    danglingStreamChecker.activeStreams.removeFirstMatchingValue (this);
   #endif
}

//==============================================================================
bool OutputStream::writeBool (const bool b)
{
    return writeByte (b ? (char) 1
                        : (char) 0);
}

bool OutputStream::writeByte (char byte)
{
    return write (&byte, 1);
}

bool OutputStream::writeRepeatedByte (uint8 byte, size_t numTimesToRepeat)
{
    for (size_t i = 0; i < numTimesToRepeat; ++i)
        if (! writeByte ((char) byte))
            return false;

    return true;
}

bool OutputStream::writeShort (short value)
{
    const unsigned short v = ByteOrder::swapIfBigEndian ((unsigned short) value);
    return write (&v, 2);
}

bool OutputStream::writeShortBigEndian (short value)
{
    const unsigned short v = ByteOrder::swapIfLittleEndian ((unsigned short) value);
    return write (&v, 2);
}

bool OutputStream::writeInt32 (int32 value)
{
    static_bassert (sizeof (int32) == 4);

    const unsigned int v = ByteOrder::swapIfBigEndian ((uint32) value);
    return write (&v, 4);
}

bool OutputStream::writeInt (int value)
{
    static_bassert (sizeof (int) == 4);

    const unsigned int v = ByteOrder::swapIfBigEndian ((unsigned int) value);
    return write (&v, 4);
}

bool OutputStream::writeInt32BigEndian (int value)
{
    static_bassert (sizeof (int32) == 4);
    const uint32 v = ByteOrder::swapIfLittleEndian ((uint32) value);
    return write (&v, 4);
}

bool OutputStream::writeIntBigEndian (int value)
{
    static_bassert (sizeof (int) == 4);
    const unsigned int v = ByteOrder::swapIfLittleEndian ((unsigned int) value);
    return write (&v, 4);
}

bool OutputStream::writeCompressedInt (int value)
{
    unsigned int un = (value < 0) ? (unsigned int) -value
                                  : (unsigned int) value;

    uint8 data[5];
    int num = 0;

    while (un > 0)
    {
        data[++num] = (uint8) un;
        un >>= 8;
    }

    data[0] = (uint8) num;

    if (value < 0)
        data[0] |= 0x80;

    return write (data, (size_t) num + 1);
}

bool OutputStream::writeInt64 (int64 value)
{
    const uint64 v = ByteOrder::swapIfBigEndian ((uint64) value);
    return write (&v, 8);
}

bool OutputStream::writeInt64BigEndian (int64 value)
{
    const uint64 v = ByteOrder::swapIfLittleEndian ((uint64) value);
    return write (&v, 8);
}

bool OutputStream::writeFloat (float value)
{
    union { int asInt; float asFloat; } n;
    n.asFloat = value;
    return writeInt (n.asInt);
}

bool OutputStream::writeFloatBigEndian (float value)
{
    union { int asInt; float asFloat; } n;
    n.asFloat = value;
    return writeIntBigEndian (n.asInt);
}

bool OutputStream::writeDouble (double value)
{
    union { int64 asInt; double asDouble; } n;
    n.asDouble = value;
    return writeInt64 (n.asInt);
}

bool OutputStream::writeDoubleBigEndian (double value)
{
    union { int64 asInt; double asDouble; } n;
    n.asDouble = value;
    return writeInt64BigEndian (n.asInt);
}

bool OutputStream::writeString (const String& text)
{
    // (This avoids using toUTF8() to prevent the memory bloat that it would leave behind
    // if lots of large, persistent strings were to be written to streams).
    const size_t numBytes = text.getNumBytesAsUTF8() + 1;
    HeapBlock<char> temp (numBytes);
    text.copyToUTF8 (temp, numBytes);
    return write (temp, numBytes);
}

bool OutputStream::writeText (const String& text, const bool asUTF16,
                              const bool writeUTF16ByteOrderMark)
{
    if (asUTF16)
    {
        if (writeUTF16ByteOrderMark)
            write ("\x0ff\x0fe", 2);

        String::CharPointerType src (text.getCharPointer());
        bool lastCharWasReturn = false;

        for (;;)
        {
            const beast_wchar c = src.getAndAdvance();

            if (c == 0)
                break;

            if (c == '\n' && ! lastCharWasReturn)
                writeShort ((short) '\r');

            lastCharWasReturn = (c == L'\r');

            if (! writeShort ((short) c))
                return false;
        }
    }
    else
    {
        const char* src = text.toUTF8();
        const char* t = src;

        for (;;)
        {
            if (*t == '\n')
            {
                if (t > src)
                    if (! write (src, (size_t) (t - src)))
                        return false;

                if (! write ("\r\n", 2))
                    return false;

                src = t + 1;
            }
            else if (*t == '\r')
            {
                if (t[1] == '\n')
                    ++t;
            }
            else if (*t == 0)
            {
                if (t > src)
                    if (! write (src, (size_t) (t - src)))
                        return false;

                break;
            }

            ++t;
        }
    }

    return true;
}

int OutputStream::writeFromInputStream (InputStream& source, int64 numBytesToWrite)
{
    if (numBytesToWrite < 0)
        numBytesToWrite = std::numeric_limits<int64>::max();

    int numWritten = 0;

    while (numBytesToWrite > 0)
    {
        char buffer [8192];
        const int num = source.read (buffer, (int) bmin (numBytesToWrite, (int64) sizeof (buffer)));

        if (num <= 0)
            break;

        write (buffer, (size_t) num);

        numBytesToWrite -= num;
        numWritten += num;
    }

    return numWritten;
}

//==============================================================================
void OutputStream::setNewLineString (const String& newLineString_)
{
    newLineString = newLineString_;
}

//==============================================================================
BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const int number)
{
    return stream << String (number);
}

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const int64 number)
{
    return stream << String (number);
}

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const double number)
{
    return stream << String (number);
}

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const char character)
{
    stream.writeByte (character);
    return stream;
}

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const char* const text)
{
    stream.write (text, strlen (text));
    return stream;
}

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const MemoryBlock& data)
{
    if (data.getSize() > 0)
        stream.write (data.getData(), data.getSize());

    return stream;
}

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const File& fileToRead)
{
    FileInputStream in (fileToRead);

    if (in.openedOk())
        return stream << in;

    return stream;
}

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, InputStream& streamToRead)
{
    stream.writeFromInputStream (streamToRead, -1);
    return stream;
}

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const NewLine&)
{
    return stream << stream.getNewLineString();
}

//------------------------------------------------------------------------------

// Unfortunately, putting these in the header causes duplicate
// definition linker errors, even with the inline keyword!

template <>
BEAST_API bool OutputStream::writeType <char> (char v) { return writeByte (v); }

template <>
BEAST_API bool OutputStream::writeType <short> (short v) { return writeShort (v); }

template <>
BEAST_API bool OutputStream::writeType <int32> (int32 v) { return writeInt32 (v); }

template <>
BEAST_API bool OutputStream::writeType <int64> (int64 v) { return writeInt64 (v); }

template <>
BEAST_API bool OutputStream::writeType <unsigned char> (unsigned char v) { return writeByte (static_cast <char> (v)); }

template <>
BEAST_API bool OutputStream::writeType <unsigned short> (unsigned short v) { return writeShort (static_cast <short> (v)); }

template <>
BEAST_API bool OutputStream::writeType <uint32> (uint32 v) { return writeInt32 (static_cast <int32> (v)); }

template <>
BEAST_API bool OutputStream::writeType <uint64> (uint64 v) { return writeInt64 (static_cast <int64> (v)); }

template <>
BEAST_API bool OutputStream::writeType <float> (float v) { return writeFloat (v); }

template <>
BEAST_API bool OutputStream::writeType <double> (double v) { return writeDouble (v); }

//------------------------------------------------------------------------------

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <char> (char v) { return writeByte (v); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <short> (short v) { return writeShortBigEndian (v); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <int32> (int32 v) { return writeInt32BigEndian (v); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <int64> (int64 v) { return writeInt64BigEndian (v); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <unsigned char> (unsigned char v) { return writeByte (static_cast <char> (v)); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <unsigned short> (unsigned short v) { return writeShortBigEndian (static_cast <short> (v)); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <uint32> (uint32 v) { return writeInt32BigEndian (static_cast <int32> (v)); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <uint64> (uint64 v) { return writeInt64BigEndian (static_cast <int64> (v)); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <float> (float v) { return writeFloatBigEndian (v); }

template <>
BEAST_API bool OutputStream::writeTypeBigEndian <double> (double v) { return writeDoubleBigEndian (v); }

BEAST_API OutputStream& BEAST_CALLTYPE operator<< (OutputStream& stream, const String& text)
{
    const size_t numBytes = text.getNumBytesAsUTF8();

   #if (BEAST_STRING_UTF_TYPE == 8)
    stream.write (text.getCharPointer().getAddress(), numBytes);
   #else
    // (This avoids using toUTF8() to prevent the memory bloat that it would leave behind
    // if lots of large, persistent strings were to be written to streams).
    HeapBlock<char> temp (numBytes + 1);
    CharPointer_UTF8 (temp).writeAll (text.getCharPointer());
    stream.write (temp, numBytes);
   #endif

    return stream;
}
