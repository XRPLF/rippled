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

#include <ripple/ledger/ReadView.h>

namespace ripple {

ReadView::sles_type::sles_type(ReadView const& view) : ReadViewFwdRange(view)
{
}

auto
ReadView::sles_type::begin() const -> iterator
{
    return iterator(view_, view_->slesBegin());
}

auto
ReadView::sles_type::end() const -> iterator
{
    return iterator(view_, view_->slesEnd());
}

auto
ReadView::sles_type::upper_bound(key_type const& key) const -> iterator
{
    return iterator(view_, view_->slesUpperBound(key));
}

ReadView::txs_type::txs_type(ReadView const& view) : ReadViewFwdRange(view)
{
}

bool
ReadView::txs_type::empty() const
{
    return begin() == end();
}

auto
ReadView::txs_type::begin() const -> iterator
{
    return iterator(view_, view_->txsBegin());
}

auto
ReadView::txs_type::end() const -> iterator
{
    return iterator(view_, view_->txsEnd());
}

Rules
makeRulesGivenLedger(
    DigestAwareReadView const& ledger,
    std::unordered_set<uint256, beast::uhash<>> const& presets)
{
    Keylet const k = keylet::amendments();
    std::optional digest = ledger.digest(k.key);
    if (digest)
    {
        auto const sle = ledger.read(k);
        if (sle)
            return Rules(presets, digest, sle->getFieldV256(sfAmendments));
    }
    return Rules(presets);
}

}  // namespace ripple
