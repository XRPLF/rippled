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
#include <type_traits>

namespace ripple {

/** Stir the RNG using entropy from stable storage.

    @param file the file from which state is loaded and into
           which it is saved.

    @return true if the pool has sufficient entropy; false
            otherwise.
*/
bool stir_entropy (std::string file);

/** Adds entropy to the RNG pool.

    @param buffer An optional buffer that contains random data.
    @param count The size of the buffer, in bytes (or 0).

    This can be called multiple times to stir entropy into the pool
    without any locks.
*/
void add_entropy (void* buffer = nullptr, int count = 0);

/** Generate random bytes, suitable for cryptography. */
/**@{*/
/**
    @param buffer The place to store the bytes.
    @param count The number of bytes to generate.
*/
void random_fill (void* buffer, int count);

/** Fills a POD object with random data */
template <class T, class = std::enable_if_t<std::is_pod<T>::value>>
void
random_fill (T* object)
{
    random_fill (object, sizeof (T));
}
/**@}*/

}

#endif
