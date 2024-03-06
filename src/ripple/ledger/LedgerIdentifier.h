//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#ifndef RIPPLE_LEDGER_LEDGERIDENTIFIER_H_INCLUDED
#define RIPPLE_LEDGER_LEDGERIDENTIFIER_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/hash/hash_append.h>
#include <ripple/protocol/Protocol.h>

#include <cstdint>
#include <ostream>
#include <type_traits>

namespace ripple {

using LedgerSequence = LedgerIndex;
using LedgerDigest = uint256;

// This is a struct and not a std::pair or std::tuple so that we get nice
// member names.
struct LedgerIdentifier
{
    LedgerSequence sequence;
    LedgerDigest digest;

    template <typename Hasher>
    friend void
    hash_append(Hasher& h, LedgerIdentifier const& id) noexcept
    {
        using beast::hash_append;
        hash_append(h, id.digest);
        hash_append(h, id.sequence);
    }
};

inline bool
operator==(LedgerIdentifier const& lhs, LedgerIdentifier const& rhs)
{
    return std::tie(lhs.sequence, lhs.digest) ==
        std::tie(rhs.sequence, rhs.digest);
}

inline std::ostream&
operator<<(std::ostream& out, LedgerIdentifier const& id)
{
    return out << "#" << id.sequence << " (" << id.digest << ")";
}

using ObjectDigest = uint256;

}  // namespace ripple

#endif
