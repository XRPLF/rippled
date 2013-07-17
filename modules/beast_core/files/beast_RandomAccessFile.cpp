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

RandomAccessFile::RandomAccessFile (int bufferSizeToUse) noexcept
    : fileHandle (nullptr)
    , currentPosition (0)
    , bufferSize (bufferSizeToUse)
    , bytesInBuffer (0)
    , writeBuffer (bmax (bufferSizeToUse, 16)) // enforce minimum size of 16
{
}

RandomAccessFile::~RandomAccessFile ()
{
    close ();
}

Result RandomAccessFile::open (File const& path, Mode mode)
{
    close ();

    return nativeOpen (path, mode);
}

void RandomAccessFile::close ()
{
    if (isOpen ())
    {
        flushBuffer ();
        nativeFlush ();
        nativeClose ();
    }
}

Result RandomAccessFile::setPosition (FileOffset newPosition)
{
    Result result (Result::ok ());

    if (newPosition != currentPosition)
    {
        flushBuffer ();

        result = nativeSetPosition (newPosition);
    }

    return result;
}

Result RandomAccessFile::read (void* buffer, ByteCount numBytes, ByteCount* pActualAmount)
{
    return nativeRead (buffer, numBytes, pActualAmount);
}

Result RandomAccessFile::write (const void* data, ByteCount numBytes, ByteCount* pActualAmount)
{
    bassert (data != nullptr && ((ssize_t) numBytes) >= 0);

    Result result (Result::ok ());

    ByteCount amountWritten = 0;

    if (bytesInBuffer + numBytes < bufferSize)
    {
        memcpy (writeBuffer + bytesInBuffer, data, numBytes);
        bytesInBuffer += numBytes;
        currentPosition += numBytes;
    }
    else
    {
        result = flushBuffer ();

        if (result.wasOk ())
        {
            if (numBytes < bufferSize)
            {
                bassert (bytesInBuffer == 0);

                memcpy (writeBuffer + bytesInBuffer, data, numBytes);
                bytesInBuffer += numBytes;
                currentPosition += numBytes;
            }
            else
            {
                ByteCount bytesWritten;

                result = nativeWrite (data, numBytes, &bytesWritten);

                if (result.wasOk ())
                    currentPosition += bytesWritten;
            }
        }
    }

    if (pActualAmount != nullptr)
        *pActualAmount = amountWritten;

    return result;
}

Result RandomAccessFile::truncate ()
{
    Result result = flush ();

    if (result.wasOk ())
        result = nativeTruncate ();

    return result;
}

Result RandomAccessFile::flush ()
{
    Result result = flushBuffer ();

    if (result.wasOk ())
        result = nativeFlush ();

    return result;
}

Result RandomAccessFile::flushBuffer ()
{
    bassert (isOpen ());

    Result result (Result::ok ());

    if (bytesInBuffer > 0)
    {
        result = nativeWrite (writeBuffer, bytesInBuffer);
        bytesInBuffer = 0;
    }

    return result;
}

//------------------------------------------------------------------------------

class RandomAccessFileTests : public UnitTest
{
public:
    RandomAccessFileTests () : UnitTest ("RandomAccessFile")
    {
    }

    struct Payload
    {
        Payload (int maxBytes)
            : bufferSize (maxBytes)
            , data (maxBytes)
        {
        }

        // Create a pseudo-random payload
        void generate (int64 seedValue) noexcept
        {
            Random r (seedValue);

            bytes = 1 + r.nextInt (bufferSize);

            bassert (bytes >= 1 && bytes <= bufferSize);

            for (int i = 0; i < bytes; ++i)
                data [i] = static_cast <unsigned char> (r.nextInt ());
        }

        bool operator== (Payload const& other) const noexcept
        {
            if (bytes == other.bytes)
            {
                return memcmp (data.getData (), other.data.getData (), bytes) == 0;
            }
            else
            {
                return false;
            }
        }

        int const bufferSize;
        int bytes;
        HeapBlock <char> data;
    };

    void runTest ()
    {
        RandomAccessFile file;

        beginTest ("open");

        Result result = file.open (File::createTempFile ("tests"), RandomAccessFile::readWrite);

        expect (result.wasOk (), "Should be ok");
    }

private:
};

static RandomAccessFileTests randomAccessFileTests;
