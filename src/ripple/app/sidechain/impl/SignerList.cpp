//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#include <ripple/app/sidechain/impl/SignerList.h>

namespace ripple {
namespace sidechain {

SignerList::SignerList(
    AccountID const& account,
    hash_set<PublicKey> const& signers,
    beast::Journal j)
    : account_(account)
    , signers_(signers)
    , quorum_(static_cast<std::uint32_t>(std::ceil(signers.size() * 0.8)))
    , j_(j)
{
    (void)j_;
}

bool
SignerList::isFederator(PublicKey const& pk) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return signers_.find(pk) != signers_.end();
}

std::uint32_t
SignerList::quorum() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return quorum_;
}
}  // namespace sidechain
}  // namespace ripple
