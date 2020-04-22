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
#include <boost/optional.hpp>

namespace ripple {

class Rules::Impl
{
private:
    std::unordered_set<uint256, hardened_hash<>> set_;
    boost::optional<uint256> digest_;
    std::unordered_set<uint256, beast::uhash<>> const& presets_;

public:
    explicit Impl(std::unordered_set<uint256, beast::uhash<>> const& presets)
        : presets_(presets)
    {
    }

    explicit Impl(
        DigestAwareReadView const& ledger,
        std::unordered_set<uint256, beast::uhash<>> const& presets)
        : presets_(presets)
    {
        auto const k = keylet::amendments();
        digest_ = ledger.digest(k.key);
        if (!digest_)
            return;
        auto const sle = ledger.read(k);
        if (!sle)
        {
            // LogicError() ?
            return;
        }

        for (auto const& item : sle->getFieldV256(sfAmendments))
            set_.insert(item);
    }

    bool
    enabled(uint256 const& feature) const
    {
        if (presets_.count(feature) > 0)
            return true;
        return set_.count(feature) > 0;
    }

    bool
    changed(DigestAwareReadView const& ledger) const
    {
        auto const digest = ledger.digest(keylet::amendments().key);
        if (!digest && !digest_)
            return false;
        if (!digest || !digest_)
            return true;
        return *digest != *digest_;
    }

    bool
    operator==(Impl const& other) const
    {
        if (!digest_ && !other.digest_)
            return true;
        if (!digest_ || !other.digest_)
            return false;
        return *digest_ == *other.digest_;
    }
};

//------------------------------------------------------------------------------

Rules::Rules(
    DigestAwareReadView const& ledger,
    std::unordered_set<uint256, beast::uhash<>> const& presets)
    : impl_(std::make_shared<Impl>(ledger, presets))
{
}

Rules::Rules(std::unordered_set<uint256, beast::uhash<>> const& presets)
    : impl_(std::make_shared<Impl>(presets))
{
}

bool
Rules::enabled(uint256 const& id) const
{
    assert(impl_);
    return impl_->enabled(id);
}

bool
Rules::changed(DigestAwareReadView const& ledger) const
{
    assert(impl_);
    return impl_->changed(ledger);
}

bool
Rules::operator==(Rules const& other) const
{
    assert(impl_ && other.impl_);
    if (impl_.get() == other.impl_.get())
        return true;
    return *impl_ == *other.impl_;
}

//------------------------------------------------------------------------------

ReadView::sles_type::sles_type(ReadView const& view) : ReadViewFwdRange(view)
{
}

auto
ReadView::sles_type::begin() const -> iterator
{
    return iterator(view_, view_->slesBegin());
}

auto
ReadView::sles_type::end() const -> iterator const&
{
    if (!end_)
        end_ = iterator(view_, view_->slesEnd());
    return *end_;
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
ReadView::txs_type::end() const -> iterator const&
{
    if (!end_)
        end_ = iterator(view_, view_->txsEnd());
    return *end_;
}

}  // namespace ripple
