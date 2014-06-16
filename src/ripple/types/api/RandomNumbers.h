//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_TYPES_RANDOMNUMBERS_H
#define RIPPLE_TYPES_RANDOMNUMBERS_H

#include <beast/utility/Journal.h>

namespace ripple {

/** Cryptographically secure random number source.
*/
class RandomNumbers
{
public:
    /** Retrieve the instance of the generator.
    */
    static RandomNumbers& getInstance ();

    /** Initialize the generator.

        If the generator is not manually initialized, it will be
        automatically initialized on first use. If automatic initialization
        fails, an exception is thrown.

        @return `true` if enough entropy could be retrieved.
    */
    bool initialize (beast::Journal::Stream stream = beast::Journal::Stream());

    /** Generate secure random numbers.

        The generated data is suitable for cryptography.

        @invariant The destination buffer must be large enough or
                   undefined behavior results.

        @param destinationBuffer The place to store the bytes.
        @param numberOfBytes The number of bytes to generate.
    */
    void fillBytes (void* destinationBuffer, int numberOfBytes);

    /** Generate secure random numbers.

        The generated data is suitable for cryptography.

        Fills the memory for the object with random numbers.
        This is a type-safe alternative to the function above.

        @param object A pointer to the object to fill.

        @tparam T The type of `object`

        @note Undefined behavior results if `T` is not a POD type.
    */
    template <class T>
    void fill (T* object)
    {
        fillBytes (object, sizeof (T));
    }

private:
    RandomNumbers ();

    ~RandomNumbers ();

    bool platformAddEntropy (beast::Journal::Stream stream);

    void platformAddPerformanceMonitorEntropy ();

private:
    bool m_initialized;
};

}

#endif
