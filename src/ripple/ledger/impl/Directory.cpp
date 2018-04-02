//------------  ------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2015 Ripple Labs Inc.

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

#include <ripple/ledger/Directory.h>

namespace ripple {

using const_iterator = Dir::const_iterator;

Dir::Dir(ReadView const& view,
        Keylet const& key)
    : view_(&view)
    , root_(key)
    , sle_(view_->read(root_))
{
    if (sle_ != nullptr)
        indexes_ = &sle_->getFieldV256(sfIndexes);
}

auto
Dir::begin() const ->
    const_iterator
{
    auto it = const_iterator(*view_, root_, root_);
    if (sle_ != nullptr)
    {
        it.sle_ = sle_;
        if (! indexes_->empty())
        {
            it.indexes_ = indexes_;
            it.it_ = std::begin(*indexes_);
            it.index_ = *it.it_;
        }
    }

    return it;
}

auto
Dir::end() const  ->
    const_iterator
{
    return const_iterator(*view_, root_, root_);
}

bool
const_iterator::operator==(const_iterator const& other) const
{
    if (view_ == nullptr || other.view_ == nullptr)
        return false;

    assert(view_ == other.view_ && root_.key == other.root_.key);
    return page_.key == other.page_.key && index_ == other.index_;
}

const_iterator::reference
const_iterator::operator*() const
{
    assert(index_ != zero);
    if (! cache_)
        cache_ = view_->read(keylet::child(index_));
    return *cache_;
}

const_iterator&
const_iterator::operator++()
{
    assert(index_ != zero);
    if (++it_ != std::end(*indexes_))
    {
        index_ = *it_;
    }
    else
    {
        auto const next =
            sle_->getFieldU64(sfIndexNext);
        if (next == 0)
        {
            page_.key = root_.key;
            index_ = zero;
        }
        else
        {
            page_ = keylet::page(root_, next);
            sle_ = view_->read(page_);
            assert(sle_);
            indexes_ = &sle_->getFieldV256(sfIndexes);
            if (indexes_->empty())
            {
                index_ = zero;
            }
            else
            {
                it_ = std::begin(*indexes_);
                index_ = *it_;
            }
        }
    }

    cache_ = boost::none;
    return *this;
}

const_iterator
const_iterator::operator++(int)
{
    assert(index_ != zero);
    const_iterator tmp(*this);
    ++(*this);
    return tmp;
}

} // ripple
