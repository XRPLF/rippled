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

#include <beast/unit_test/suite.h>

namespace beast {

RandomAccessFile::RandomAccessFile () noexcept
    : fileHandle (nullptr)
    , currentPosition (0)
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
        nativeFlush ();
        nativeClose ();
    }
}

Result RandomAccessFile::setPosition (FileOffset newPosition)
{
    if (newPosition != currentPosition)
    {
        // VFALCO NOTE I dislike return from the middle but
        //             Result::ok() is showing up in the profile
        //
        return nativeSetPosition (newPosition);
    }

    return Result::ok ();
}

Result RandomAccessFile::read (void* buffer, ByteCount numBytes, ByteCount* pActualAmount)
{
    return nativeRead (buffer, numBytes, pActualAmount);
}

Result RandomAccessFile::write (const void* data, ByteCount numBytes, ByteCount* pActualAmount)
{
    bassert (data != nullptr && ((std::ptrdiff_t) numBytes) >= 0);

    Result result (Result::ok ());

    ByteCount amountWritten = 0;

    result = nativeWrite (data, numBytes, &amountWritten);

    if (result.wasOk ())
        currentPosition += amountWritten;

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
    return nativeFlush ();
}

//------------------------------------------------------------------------------

class RandomAccessFile_test : public unit_test::suite
{
public:
    enum
    {
        maxPayload = 8192
    };

    /*  For this test we will create a file which consists of a fixed
        number of variable length records. Each record is numbered sequentially
        starting at 0. To calculate the position of each record we first build
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
                               std::int64_t seedValue)
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

    // Write all the records to the file.
    // The payload is pseudo-randomly generated.
    void writeRecords (RandomAccessFile& file,
                       int numRecords,
                       HeapBlock <Record> const& records,
                       std::int64_t seedValue)
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

    // Read the records and verify the consistency.
    void readRecords (RandomAccessFile& file,
                      int numRecords,
                      HeapBlock <Record> const& records,
                      std::int64_t seedValue)
    {
        using namespace UnitTestUtilities;

        for (int i = 0; i < numRecords; ++i)
        {
            Record const& record (records [i]);

            int const bytes = record.bytes;

            Payload p1 (bytes);
            Payload p2 (bytes);

            p1.repeatableRandomFill (bytes, bytes, record.index + seedValue);

            file.setPosition (record.offset);

            Result result = file.read (p2.data.getData (), bytes);

            expect (result.wasOk (), "Should be ok");

            if (result.wasOk ())
            {
                p2.bytes = bytes;

                expect (p1 == p2, "Should be equal");
            }
        }
    }

    // Perform the test at the given buffer size.
    void testFile (int const numRecords)
    {
        using namespace UnitTestUtilities;

        int const seedValue = 50;

        std::stringstream ss;
        ss << numRecords << " records";
        testcase (ss.str());

        // Calculate the path
        File const path (File::createTempFile ("RandomAccessFile"));

        // Create a predictable set of records
        HeapBlock <Record> records (numRecords);
        createRecords (records, numRecords, maxPayload, seedValue);

        Result result (Result::ok ());

        {
            // Create the file
            RandomAccessFile file;
            result = file.open (path, RandomAccessFile::readWrite);
            expect (result.wasOk (), "Should be ok");

            if (result.wasOk ())
            {
                writeRecords (file, numRecords, records, seedValue);

                readRecords (file, numRecords, records, seedValue);

                repeatableShuffle (numRecords, records, seedValue);

                readRecords (file, numRecords, records, seedValue);
            }
        }

        if (result.wasOk ())
        {
            // Re-open the file in read only mode
            RandomAccessFile file;
            result = file.open (path, RandomAccessFile::readOnly);
            expect (result.wasOk (), "Should be ok");

            if (result.wasOk ())
            {
                readRecords (file, numRecords, records, seedValue);
            }
        }
    }

    void run ()
    {
        testFile (10000);
    }
};

BEAST_DEFINE_TESTSUITE(RandomAccessFile,beast_core,beast);

} // beast
