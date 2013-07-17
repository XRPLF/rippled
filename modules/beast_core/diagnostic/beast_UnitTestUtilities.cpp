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

class UnitTestUtilitiesTests : public UnitTest
{
public:
    UnitTestUtilitiesTests () : UnitTest ("UnitTestUtilities")
    {
    }

    void testPayload ()
    {
        using namespace UnitTestUtilities;

        int const maxBufferSize = 4000;
        int const minimumBytes = 1;
        int const numberOfItems = 100;
        int64 const seedValue = 50;

        beginTest ("Payload");

        Payload p1 (maxBufferSize);
        Payload p2 (maxBufferSize);

        for (int i = 0; i < numberOfItems; ++i)
        {
            p1.repeatableRandomFill (minimumBytes, maxBufferSize, seedValue);
            p2.repeatableRandomFill (minimumBytes, maxBufferSize, seedValue);

            expect (p1 == p2, "Should be equal");
        }
    }

    void runTest ()
    {
        testPayload ();
    }
};

static UnitTestUtilitiesTests unitTestUtilitiesTests;
