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

#ifndef BEAST_UNITTESTUTILITIES_H_INCLUDED
#define BEAST_UNITTESTUTILITIES_H_INCLUDED

namespace UnitTestUtilities
{

/** Fairly shuffle an array pseudo-randomly.
*/
template <class T>
void repeatableShuffle (int const numberOfItems, T& arrayOfItems, Random& r)
{
    for (int i = numberOfItems - 1; i > 0; --i)
    {
        int const choice = r.nextInt (i + 1);
        std::swap (arrayOfItems [i], arrayOfItems [choice]);
    }
}

template <class T>
void repeatableShuffle (int const numberOfItems, T& arrayOfItems, int64 seedValue)
{
    Random r (seedValue);
    repeatableShuffle (numberOfItems, arrayOfItems, r);
}

//------------------------------------------------------------------------------

/** A block of memory used for test data.
*/
struct Payload
{
    /** Construct a payload with a buffer of the specified maximum size.

        @param maximumBytes The size of the buffer, in bytes.
    */
    explicit Payload (int maxBufferSize)
        : bufferSize (maxBufferSize)
        , data (maxBufferSize)
    {
    }

    /** Generate a random block of data within a certain size range.

        @param minimumBytes The smallest number of bytes in the resulting payload.
        @param maximumBytes The largest number of bytes in the resulting payload.
        @param seedValue The value to seed the random number generator with.
    */
    void repeatableRandomFill (int minimumBytes, int maximumBytes, int64 seedValue) noexcept
    {
        bassert (minimumBytes >=0 && maximumBytes <= bufferSize);

        Random r (seedValue);

        bytes = minimumBytes + r.nextInt (1 + maximumBytes - minimumBytes);

        bassert (bytes >= minimumBytes && bytes <= bufferSize);

        for (int i = 0; i < bytes; ++i)
            data [i] = static_cast <unsigned char> (r.nextInt ());
    }

    /** Compare two payloads for equality.
    */
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

public:
    int const bufferSize;

    int bytes;
    HeapBlock <char> data;
};

//------------------------------------------------------------------------------

/** Format unit test results in JUnit XML format.

    The output can be used directly with the Jenkins CI server with
    the appropriate JUnit plugin.

    JUnit FAQ:      http://junit.sourceforge.net/doc/faq/faq.htm

    Jenkins Home:   http://jenkins-ci.org/

    @see UnitTest, UnitTests
*/

class JUnitXMLFormatter : public Uncopyable
{
public:
    JUnitXMLFormatter (UnitTests const& tests);

    String createDocumentString ();

private:
    static String timeToString (Time const& time);
    static String secondsToString (double seconds);

private:
    UnitTests const& m_tests;
    String const m_currentTime;
    String const m_hostName;
};

}

#endif
