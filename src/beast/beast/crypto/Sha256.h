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

#ifndef BEAST_CRYPTO_Sha256_H_INCLUDED
#define BEAST_CRYPTO_Sha256_H_INCLUDED

#include "../config/PlatformConfig.h"
#include "../CStdInt.h"
#include "../FixedArray.h"

//------------------------------------------------------------------------------

namespace beast {
namespace Sha256 {

enum
{
    digestLength = 32,
    blockLength = 64
};

/** A container suitable for holding the resulting hash. */
typedef FixedArray <uint8, digestLength> digest_type;

namespace detail {
struct Context
{
	beast::uint32 state[8];
	beast::uint64 bitcount;
	beast::uint8  buffer[Sha256::blockLength];
};
}

/** Computes the Sha256 hash of data. */
class Context
{
public:
    /** Create a new hasher prepared for input. */
    Context();

    /** Update the hashing context with the input sequence. */
    /** @{ */
    void update (void const* buffer, std::size_t bytes);

    void update (int8 const* begin, int8 const* end)
    {
        update (begin, end - begin);
    }

    void update (uint8 const* begin, uint8 const* end)
    {
        update (begin, end - begin);
    }

    template <typename T>
    void update (T const& t)
    {
        update (&t, sizeof(T));
    }
    /** @} */

    /** Finalize the hash process and store the digest.
        The memory pointed to by `digest` must be at least digestLength
        bytes. This object may not be re-used after calling finish.
        @return A pointer to the passed hash buffer.
    */
    /** @{ */
    void* finish (void* digest);

    digest_type& finish (digest_type& digest)
    {
        finish (digest.c_array());
        return digest;
    }

    digest_type finish ()
    {
        digest_type digest;
        finish (digest);
        return digest;
    }
    /** @} */

private:
    detail::Context m_context;
};

//------------------------------------------------------------------------------

/** Returns the hash produced by a single octet equal to zero. */
digest_type const& empty_digest();

/** Performs an entire hashing operation in a single step.
    A zero length input sequence produces the empty_digest().
    @return The resulting digest depending on the arguments.
*/
/** @{ */
void* hash (void const* buffer, std::size_t bytes, void* digest);
digest_type& hash ( void const* buffer, std::size_t bytes, digest_type& digest);
digest_type hash (void const* buffer, std::size_t bytes);
void* hash (int8 const* begin, int8 const* end, void* digest);
void* hash (uint8 const* begin, uint8 const* end, void* digest);
digest_type hash (int8 const* begin, int8 const* end);
digest_type hash (uint8 const* begin, uint8 const* end);

template <typename T>
void* hash (T const& t, void* digest)
{
    return hash (&t, sizeof(T), digest);
}

template <typename T>
digest_type& hash (T const& t, digest_type& digest)
{
    return hash (&t, sizeof(T), digest);
}

template <typename T>
digest_type hash (T const& t)
{
    digest_type digest;
    hash (&t, sizeof(T), digest);
    return digest;
}
/** @} */

/** Calculate the hash of a hash in one step.
    The memory pointed to by source_digest must be at
    least digestLength bytes or undefined behavior results.
*/
/** @{ */
void* hash (void const* source_digest, void* digest);
digest_type& hash (void const* source_digest, digest_type& digest);
digest_type hash (void const* source_digest);;
/** @} */

}
}

#endif
