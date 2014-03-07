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

#ifndef BEAST_MEMORYALIGNMENT_H_INCLUDED
#define BEAST_MEMORYALIGNMENT_H_INCLUDED

namespace beast
{

namespace Memory
{

//------------------------------------------------------------------------------

// Constants
//
// These need to be set based on the target CPU
//

const int cacheLineAlignBits  = 6; // 64 bytes
const int cacheLineAlignBytes = 1 << cacheLineAlignBits;
const int cacheLineAlignMask  = cacheLineAlignBytes - 1;

const int allocAlignBits  = 3; // 8 bytes
const int allocAlignBytes = 1 << allocAlignBits;
const int allocAlignMask  = allocAlignBytes - 1;

//------------------------------------------------------------------------------

// Returns the number of bytes needed to advance p to the correct alignment
template <typename P>
inline size_t bytesNeededForAlignment (P const* const p)
{
    return (allocAlignBytes - (uintptr_t (p) & allocAlignMask))
           & allocAlignMask;
}

// Returns the number of bytes to make "bytes" an aligned size
inline size_t sizeAdjustedForAlignment (const size_t bytes)
{
    return (bytes + allocAlignMask) & ~allocAlignMask;
}

// Returns a pointer with alignment added.
template <typename P>
inline P* pointerAdjustedForAlignment (P* const p)
{
    return reinterpret_cast <P*> (reinterpret_cast <char*> (p) +
                                  bytesNeededForAlignment (p));
}

}  // namespace Memory

}  // namespace beast

#endif
