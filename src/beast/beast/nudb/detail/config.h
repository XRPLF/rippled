//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NUDB_CONFIG_H_INCLUDED
#define BEAST_NUDB_CONFIG_H_INCLUDED

#include <beast/hash/xxhasher.h>

// Compiles out domain checks
#ifndef BEAST_NUDB_NO_DOMAIN_CHECK
# ifdef NDEBUG
#  define BEAST_NUDB_NO_DOMAIN_CHECK 1
# else
#  define BEAST_NUDB_NO_DOMAIN_CHECK 0
# endif
#endif

namespace beast {
namespace nudb {

// xxhasher is the fastest and the best choice
// when keys are already uniformly distributed
using default_hash = xxhasher;

namespace detail {

// Returns the closest power of 2 not less than x
template <class = void>
std::size_t
ceil_pow2 (unsigned long long x)
{
    static const unsigned long long t[6] = {
        0xFFFFFFFF00000000ull,
        0x00000000FFFF0000ull,
        0x000000000000FF00ull,
        0x00000000000000F0ull,
        0x000000000000000Cull,
        0x0000000000000002ull
    };

    int y = (((x & (x - 1)) == 0) ? 0 : 1);
    int j = 32;
    int i;

    for(i = 0; i < 6; i++) {
        int k = (((x & t[i]) == 0) ? 0 : j);
        y += k;
        x >>= k;
        j >>= 1;
    }

    return std::size_t(1)<<y;
}

} // detail
} // nudb
} // beast

#endif
