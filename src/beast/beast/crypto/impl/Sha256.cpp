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

#include "../Sha256.h"

namespace beast {
namespace Sha256 {

#ifndef  LITTLE_ENDIAN
# define LITTLE_ENDIAN 1234
#endif
#ifndef  BIG_ENDIAN
# define BIG_ENDIAN 4321
#endif
#if !defined(BYTE_ORDER)
# if BEAST_BIG_ENDIAN
#  define BYTE_ORDER BIG_ENDIAN
# else
#  define BYTE_ORDER LITTLE_ENDIAN
# endif
#endif

//#define SHA2_USE_INTTYPES_H

namespace detail {
typedef uint8  u_int8_t;
typedef uint32 u_int32_t;
typedef uint64 u_int64_t;
#include "sha2/sha2.c"
}

Context::Context ()
{
    detail::SHA256_Init (&m_context);
}

void Context::update (void const* buffer, std::size_t bytes)
{
    detail::SHA256_Update (&m_context, static_cast <uint8 const*> (buffer), bytes);
}

void* Context::finish (void* hash)
{
    detail::SHA256_Final (static_cast <uint8*> (hash), &m_context);
    return hash;
}

//------------------------------------------------------------------------------

digest_type const& empty_digest()
{
    struct Holder
    {
        Holder ()
        {
            uint8 zero (0);
            hash (zero, digest);
        }

        digest_type digest;
    };

    static Holder const holder;

    return holder.digest;
}

void* hash (void const* buffer, std::size_t bytes, void* digest)
{
    Context h;
    h.update (buffer, bytes);
    h.finish (digest);
    return digest;
}

digest_type& hash (void const* buffer, std::size_t bytes, digest_type& digest)
{
    hash (buffer, bytes, digest.c_array());
    return digest;
}

digest_type hash (void const* buffer, std::size_t bytes)
{
    digest_type digest;
    hash (buffer, bytes, digest);
    return digest;
}

void* hash (int8 const* begin, int8 const* end, void* digest)
{
    return hash (begin, end - begin, digest);
}

void* hash (uint8 const* begin, uint8 const* end, void* digest)
{
    return hash (begin, end - begin, digest);
}

digest_type hash (int8 const* begin, int8 const* end)
{
    digest_type digest;
    hash (begin, end - begin, digest);
    return digest;
}

digest_type hash (uint8 const* begin, uint8 const* end)
{
    digest_type digest;
    hash (begin, end - begin, digest);
    return digest;
}

void* hash (void const* source_digest, void* digest)
{
    return hash (source_digest, digestLength, digest);
}

digest_type& hash (void const* source_digest, digest_type& digest)
{
    return hash (source_digest, digestLength, digest);
}

digest_type hash (void const* source_digest)
{
    digest_type digest;
    hash (source_digest, digestLength, digest);
    return digest;
}

}
}
