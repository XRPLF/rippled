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

#ifndef RIPPLE_CRYPTO_RANDOMNUMBERS_H_INCLUDED
#define RIPPLE_CRYPTO_RANDOMNUMBERS_H_INCLUDED

#include <string>
#include <beast/cxx14/type_traits.h> // <type_traits>

namespace ripple {

/** Cryptographically secure random number source.
*/
class RandomNumbers
{
public:
    /** Retrieve the instance of the generator.
    */
    static RandomNumbers& getInstance ();

    /** Generate secure random numbers suitable for cryptography.

        @param buffer The place to store the bytes.
        @param bytes The number of bytes to generate.
    */
    void fillBytes (void* buffer, int bytes);

    /** Generate secure random numbers suitable for cryptography.

        @param object pointer to a POD object to fill.
    */
    template <class T, class = std::enable_if_t<std::is_pod<T>::value>>
    void
    fill (T* object)
    {
        fillBytes (object, sizeof (T));
    }

private:
    RandomNumbers ();
    ~RandomNumbers () = default;

    bool platformAddEntropy (char *buf, size_t size, std::string& error);
};

}

#endif
