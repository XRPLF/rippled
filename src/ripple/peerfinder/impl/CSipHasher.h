//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

    Copyright (c) 2020 The Bitcoin Core developers
    Distributed under the MIT software license, see
    http://www.opensource.org/licenses/mit-license.php.

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
#ifndef RIPPLE_PEERFINDER_CSIPHASHER_H
#define RIPPLE_PEERFINDER_CSIPHASHER_H

#include <cstdint>

namespace ripple {

/** SipHash-2-4 */
class CSipHasher
{
private:
    std::uint64_t v[4];
    std::uint64_t tmp;
    int count;

public:
    /** Construct a SipHash calculator initialized with 128-bit key (k0, k1) */
    CSipHasher(std::uint64_t k0, std::uint64_t k1);
    ~CSipHasher() = default;

    /** Hash a 64-bit integer worth of data
     *  It is treated as if this was the little-endian interpretation of 8
     * bytes. This function can only be used when a multiple of 8 bytes have
     * been written so far.
     */
    CSipHasher&
    Write(std::uint64_t data);

    /** Hash arbitrary bytes. */
    CSipHasher&
    Write(const unsigned char* data, std::size_t size);

    /** Compute the 64-bit SipHash-2-4 of the data written so far. The object
     * remains untouched. */
    std::uint64_t
    Finalize() const;
};

}  // namespace ripple

#endif  // RIPPLED_CSIPHASHER_H
