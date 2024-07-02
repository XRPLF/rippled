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

#include <ripple/basics/contract.h>
#include <ripple/ledger/detail/RawStateTable.h>

namespace ripple {
namespace detail {

class RawStateTable::sles_iter_impl : public ReadView::sles_type::iter_base
{
private:
    std::shared_ptr<SLE const> sle0_;
    ReadView::sles_type::iterator iter0_;
    ReadView::sles_type::iterator end0_;
    std::shared_ptr<SLE const> sle1_;
    items_t::const_iterator iter1_;
    items_t::const_iterator end1_;

public:
    sles_iter_impl(sles_iter_impl const&) = default;

    sles_iter_impl(
        items_t::const_iterator iter1,
        items_t::const_iterator end1,
        ReadView::sles_type::iterator iter0,
        ReadView::sles_type::iterator end0)
        : iter0_(iter0), end0_(end0), iter1_(iter1), end1_(end1)
    {
        if (iter0_ != end0_)
            sle0_ = *iter0_;
        if (iter1_ != end1)
        {
            sle1_ = iter1_->second.sle;
            skip();
        }
    }

    std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<sles_iter_impl>(*this);
    }

    bool
    equal(base_type const& impl) const override
    {
        if (auto const p = dynamic_cast<sles_iter_impl const*>(&impl))
        {
            assert(end1_ == p->end1_ && end0_ == p->end0_);
            return iter1_ == p->iter1_ && iter0_ == p->iter0_;
        }

        return false;
    }

    void
    increment() override
    {
        assert(sle1_ || sle0_);

        if (sle1_ && !sle0_)
        {
            inc1();
            return;
        }

        if (sle0_ && !sle1_)
        {
            inc0();
            return;
        }

        if (sle1_->key() == sle0_->key())
        {
            inc1();
            inc0();
        }
        else if (sle1_->key() < sle0_->key())
        {
            inc1();
        }
        else
        {
            inc0();
        }
        skip();
    }

    value_type
    dereference() const override
    {
        if (!sle1_)
            return sle0_;
        else if (!sle0_)
            return sle1_;
        if (sle1_->key() <= sle0_->key())
            return sle1_;
        return sle0_;
    }

private:
    void
    inc0()
    {
        ++iter0_;
        if (iter0_ == end0_)
            sle0_ = nullptr;
        else
            sle0_ = *iter0_;
    }

    void
    inc1()
    {
        ++iter1_;
        if (iter1_ == end1_)
            sle1_ = nullptr;
        else
            sle1_ = iter1_->second.sle;
    }

    void
    skip()
    {
        while (iter1_ != end1_ && iter1_->second.action == Action::erase &&
               sle0_->key() == sle1_->key())
        {
            inc1();
            inc0();
            if (!sle0_)
                return;
        }
    }
};

//------------------------------------------------------------------------------

// Base invariants are checked by the base during apply()

void
RawStateTable::apply(RawView& to) const
{
    to.rawDestroyXRP(dropsDestroyed_);
    for (auto const& elem : items_)
    {
        auto const& item = elem.second;
        switch (item.action)
        {
            case Action::erase:
                to.rawErase(item.sle);
                break;
            case Action::insert:
                to.rawInsert(item.sle);
                break;
            case Action::replace:
                to.rawReplace(item.sle);
                break;
        }
    }
}

bool
RawStateTable::exists(ReadView const& base, Keylet const& k) const
{
    assert(k.key.isNonZero());
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.exists(k);
    auto const& item = iter->second;
    if (item.action == Action::erase)
        return false;
    if (!k.check(*item.sle))
        return false;
    return true;
}

/*  This works by first calculating succ() on the parent,
    then calculating succ() our internal list, and taking
    the lower of the two.
*/
auto
RawStateTable::succ(
    ReadView const& base,
    key_type const& key,
    std::optional<key_type> const& last) const -> std::optional<key_type>
{
    std::optional<key_type> next = key;
    items_t::const_iterator iter;
    // Find base successor that is
    // not also deleted in our list
    do
    {
        next = base.succ(*next, last);
        if (!next)
            break;
        iter = items_.find(*next);
    } while (iter != items_.end() && iter->second.action == Action::erase);
    // Find non-deleted successor in our list
    for (iter = items_.upper_bound(key); iter != items_.end(); ++iter)
    {
        if (iter->second.action != Action::erase)
        {
            // Found both, return the lower key
            if (!next || next > iter->first)
                next = iter->first;
            break;
        }
    }
    // Nothing in our list, return
    // what we got from the parent.
    if (last && next >= last)
        return std::nullopt;
    return next;
}

void
RawStateTable::erase(std::shared_ptr<SLE> const& sle)
{
    // The base invariant is checked during apply
    auto const result = items_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sle->key()),
        std::forward_as_tuple(Action::erase, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch (item.action)
    {
        case Action::erase:
            LogicError("RawStateTable::erase: already erased");
            break;
        case Action::insert:
            items_.erase(result.first);
            break;
        case Action::replace:
            item.action = Action::erase;
            item.sle = sle;
            break;
    }
}

void
RawStateTable::insert(std::shared_ptr<SLE> const& sle)
{
    auto const result = items_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sle->key()),
        std::forward_as_tuple(Action::insert, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch (item.action)
    {
        case Action::erase:
            item.action = Action::replace;
            item.sle = sle;
            break;
        case Action::insert:
            LogicError("RawStateTable::insert: already inserted");
            break;
        case Action::replace:
            LogicError("RawStateTable::insert: already exists");
            break;
    }
}

void
RawStateTable::replace(std::shared_ptr<SLE> const& sle)
{
    auto const result = items_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sle->key()),
        std::forward_as_tuple(Action::replace, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch (item.action)
    {
        case Action::erase:
            LogicError("RawStateTable::replace: was erased");
            break;
        case Action::insert:
        case Action::replace:
            item.sle = sle;
            break;
    }
}

std::shared_ptr<SLE const>
RawStateTable::read(ReadView const& base, Keylet const& k) const
{
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.read(k);
    auto const& item = iter->second;
    if (item.action == Action::erase)
        return nullptr;
    // Convert to SLE const
    std::shared_ptr<SLE const> sle = item.sle;
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

void
RawStateTable::destroyXRP(XRPAmount const& fee)
{
    dropsDestroyed_ += fee;
}

std::unique_ptr<ReadView::sles_type::iter_base>
RawStateTable::slesBegin(ReadView const& base) const
{
    return std::make_unique<sles_iter_impl>(
        items_.begin(), items_.end(), base.sles.begin(), base.sles.end());
}

std::unique_ptr<ReadView::sles_type::iter_base>
RawStateTable::slesEnd(ReadView const& base) const
{
    return std::make_unique<sles_iter_impl>(
        items_.end(), items_.end(), base.sles.end(), base.sles.end());
}

std::unique_ptr<ReadView::sles_type::iter_base>
RawStateTable::slesUpperBound(ReadView const& base, uint256 const& key) const
{
    return std::make_unique<sles_iter_impl>(
        items_.upper_bound(key),
        items_.end(),
        base.sles.upper_bound(key),
        base.sles.end());
}

}  // namespace detail
}  // namespace ripple
