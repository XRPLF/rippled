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

#include <BeastConfig.h>
#include <ripple/ledger/detail/RawStateTable.h>
#include <ripple/basics/contract.h>

namespace ripple {
namespace detail {

// Base invariants are checked by the base during apply()

void
RawStateTable::apply (RawView& to) const
{
    to.rawDestroyXRP(dropsDestroyed_);
    for (auto const& elem : items_)
    {
        auto const& item = elem.second;
        switch(item.first)
        {
        case Action::erase:
            to.rawErase(item.second);
            break;
        case Action::insert:
            to.rawInsert(item.second);
            break;
        case Action::replace:
            to.rawReplace(item.second);
            break;
        }
    }
}

bool
RawStateTable::exists (ReadView const& base,
    Keylet const& k) const
{
    assert(k.key.isNonZero());
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.exists(k);
    auto const& item = iter->second;
    if (item.first == Action::erase)
        return false;
    if (! k.check(*item.second))
        return false;
    return true;
}

/*  This works by first calculating succ() on the parent,
    then calculating succ() our internal list, and taking
    the lower of the two.
*/
auto
RawStateTable::succ (ReadView const& base,
    key_type const& key, boost::optional<
        key_type> const& last) const ->
            boost::optional<key_type>
{
    boost::optional<key_type> next = key;
    items_t::const_iterator iter;
    // Find base successor that is
    // not also deleted in our list
    do
    {
        next = base.succ(*next, last);
        if (! next)
            break;
        iter = items_.find(*next);
    }
    while (iter != items_.end() &&
        iter->second.first == Action::erase);
    // Find non-deleted successor in our list
    for (iter = items_.upper_bound(key);
        iter != items_.end (); ++iter)
    {
        if (iter->second.first != Action::erase)
        {
            // Found both, return the lower key
            if (! next || next > iter->first)
                next = iter->first;
            break;
        }
    }
    // Nothing in our list, return
    // what we got from the parent.
    if (last && next >= last)
        return boost::none;
    return next;
}

void
RawStateTable::erase(
    std::shared_ptr<SLE> const& sle)
{
    // The base invariant is checked during apply
    auto const result = items_.emplace(
        std::piecewise_construct,
            std::forward_as_tuple(sle->key()),
                std::forward_as_tuple(
                    Action::erase, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch(item.first)
    {
    case Action::erase:
        DIE("RawStateTable::erase: already erased");
        break;
    case Action::insert:
        items_.erase(result.first);
        break;
    case Action::replace:
        item.first = Action::erase;
        item.second = sle;
        break;
    }
}

void
RawStateTable::insert(
    std::shared_ptr<SLE> const& sle)
{
    auto const result = items_.emplace(
        std::piecewise_construct,
            std::forward_as_tuple(sle->key()),
                std::forward_as_tuple(
                    Action::insert, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch(item.first)
    {
    case Action::erase:
        item.first = Action::replace;
        item.second = sle;
        break;
    case Action::insert:
        DIE("RawStateTable::insert: already inserted");
        break;
    case Action::replace:
        DIE("RawStateTable::insert: already exists");
        break;
    }
}

void
RawStateTable::replace(
    std::shared_ptr<SLE> const& sle)
{
    auto const result = items_.emplace(
        std::piecewise_construct,
            std::forward_as_tuple(sle->key()),
                std::forward_as_tuple(
                    Action::replace, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch(item.first)
    {
    case Action::erase:
        DIE("RawStateTable::replace: was erased");
        break;
    case Action::insert:
    case Action::replace:
        item.second = sle;
        break;
    }
}

std::shared_ptr<SLE const>
RawStateTable::read (ReadView const& base,
    Keylet const& k) const
{
    auto const iter =
        items_.find(k.key);
    if (iter == items_.end())
        return base.read(k);
    auto const& item = iter->second;
    if (item.first == Action::erase)
        return nullptr;
    // Convert to SLE const
    std::shared_ptr<
        SLE const> sle = item.second;
    if (! k.check(*sle))
        return nullptr;
    return sle;
}

void
RawStateTable::destroyXRP(std::uint64_t feeDrops)
{
    dropsDestroyed_ += feeDrops;
}

} // detail
} // ripple
