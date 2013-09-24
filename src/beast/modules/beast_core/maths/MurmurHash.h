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

#ifndef BEAST_MURMURHASH_H_INCLUDED
#define BEAST_MURMURHASH_H_INCLUDED

// Original source code links in .cpp file

// This file depends on some Beast declarations and defines

namespace Murmur
{

extern void MurmurHash3_x86_32  (const void* key, int len, uint32 seed, void* out);
extern void MurmurHash3_x86_128 (const void* key, int len, uint32 seed, void* out);
extern void MurmurHash3_x64_128 (const void* key, int len, uint32 seed, void* out);

// Uses Beast to choose an appropriate routine

// This handy template deduces which size hash is desired
template <typename HashType>
inline void Hash (const void* key, int len, uint32 seed, HashType* out)
{
    switch (8 * sizeof (HashType))
    {
    case 32:
        MurmurHash3_x86_32 (key, len, seed, out);
        break;

#if BEAST_64BIT
    case 128:
        MurmurHash3_x64_128 (key, len, seed, out);
        break;

#else
    case 128:
        MurmurHash3_x86_128 (key, len, seed, out);
        break;

#endif

    default:
        Throw (std::runtime_error ("invalid key size in MurmurHash"));
        break;
    };
}

}

#endif
