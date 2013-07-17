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
    RandomAccessFileTests ()
        : UnitTest ("RandomAccessFile")
        , numRecords (1000)
        , seedValue (50)
    {
    }

    /*  For this test we will create a file which consists of a fixed
        number of variable length records. Each record is numbered sequentially
        start at 1. To calculate the position of each record we first build
        a table of size/offset pairs using a pseudorandom number generator.
    */
    struct Record
    {
        int index;
        int bytes;
        int offset;
    };

    typedef HeapBlock <Record> Records;

    // Produce the pseudo-random set of records.
    static void createRecords (HeapBlock <Record>& records,
                               int numRecords,
                               int maxBytes,
                               int64 seedValue)
    {
        using namespace UnitTestUtilities;

        Random r (seedValue);

        records.malloc (numRecords);

        int offset = 0;

        for (int i = 0; i < numRecords; ++i)
        {
            int const bytes = r.nextInt (maxBytes) + 1;

            records [i].index = i;
            records [i].bytes = bytes;
            records [i].offset = offset;

            offset += bytes;
        }

        repeatableShuffle (numRecords, records, seedValue);
    }

    void writeRecords (RandomAccessFile& file,
                       int numRecords,
                       HeapBlock <Record> const& records,
                       int64 seedValue)
    {
        using namespace UnitTestUtilities;

        for (int i = 0; i < numRecords; ++i)
        {
            Payload p (records [i].bytes);

            p.repeatableRandomFill (records [i].bytes,
                                    records [i].bytes,
                                    records [i].index + seedValue);

            file.setPosition (records [i].offset);

            Result result = file.write (p.data.getData (), p.bytes);

            expect (result.wasOk (), "Should be ok");
        }
    }

    void readRecords (RandomAccessFile& file,
                      int numRecords,
                      HeapBlock <Record>const & records,
                      int64 seedValue)
    {
        using namespace UnitTestUtilities;

        for (int i = 0; i < numRecords; ++i)
        {
            int const bytes = records [i].bytes;

            Payload p1 (bytes);
            Payload p2 (bytes);

            p1.repeatableRandomFill (bytes, bytes, records [i].index + seedValue);

            file.setPosition (records [i].offset);

            Result result = file.read (p2.data.getData (), bytes);

            expect (result.wasOk (), "Should be ok");

            if (result.wasOk ())
            {
                p2.bytes = bytes;

                expect (p1 == p2, "Should be equal");
            }
        }
    }

    void testFile (int const bufferSize)
    {
        using namespace UnitTestUtilities;

        String s;
        s << "bufferSize = " << String (bufferSize);
        beginTest (s);

        int const maxPayload = bmax (1000, bufferSize * 2);

        RandomAccessFile file (bufferSize);

        Result result = file.open (File::createTempFile ("tests"), RandomAccessFile::readWrite);

        expect (result.wasOk (), "Should be ok");

        if (result.wasOk ())
        {
            HeapBlock <Record> records (numRecords);

            createRecords (records, numRecords, maxPayload, seedValue);

            writeRecords (file, numRecords, records, seedValue);

            readRecords (file, numRecords, records, seedValue);

            repeatableShuffle (numRecords, records, seedValue);

            readRecords (file, numRecords, records, seedValue);
        }
    }

    void runTest ()
    {
        testFile (0);
        testFile (1000);
        testFile (10000);
    }

private:
    int const numRecords;
    int64 const seedValue;
};

static RandomAccessFileTests randomAccessFileTests;
