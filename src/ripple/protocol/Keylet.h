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

#ifndef RIPPLE_PROTOCOL_KEYLET_H_INCLUDED
#define RIPPLE_PROTOCOL_KEYLET_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/protocol/AcctRoot.h>
#include <ripple/protocol/LedgerFormats.h>

namespace ripple {

class STLedgerEntry;

/** A pair of SHAMap key and LedgerEntryType.

    A Keylet identifies both a key in the state map and its ledger entry type.

    @note Keylet is a portmanteau of the words key and LET, an acronym for
          LedgerEntryType.
*/
struct KeyletBase
{
    LedgerEntryType type;
    uint256 key;

protected:
    // Only derived classes should be able to construct a KeyletBase.
    KeyletBase(LedgerEntryType type_, uint256 const& key_)
        : type(type_), key(key_)
    {
    }

    // You should be able to copy construct derived classes.
    KeyletBase(KeyletBase const&) = default;
    KeyletBase(KeyletBase&&) = default;

    // You should be able to assign derived classes.
    KeyletBase&
    operator=(KeyletBase const&) = default;
    KeyletBase&
    operator=(KeyletBase&&) = default;

    // The destructor is protected so derived classes cannot be destroyed
    // through a pointer to the base.  It also allows us to do derivation
    // without needing a virtual table.
    ~KeyletBase() = default;

public:
    /** Returns true if the SLE matches the type */
    bool
    check(STLedgerEntry const&) const;
};

#ifndef __INTELLISENSE__
static_assert(!std::is_default_constructible_v<KeyletBase>);
static_assert(!std::is_copy_constructible_v<KeyletBase>);
static_assert(!std::is_move_constructible_v<KeyletBase>);
static_assert(!std::is_copy_assignable_v<KeyletBase>);
static_assert(!std::is_move_assignable_v<KeyletBase>);
static_assert(!std::is_nothrow_destructible_v<KeyletBase>);
#endif

struct Keylet final : public KeyletBase
{
    Keylet(LedgerEntryType type, uint256 const& key) : KeyletBase(type, key)
    {
    }

    using KeyletBase::check;
};

#ifndef __INTELLISENSE__
static_assert(!std::is_default_constructible_v<Keylet>);
static_assert(std::is_copy_constructible_v<Keylet>);
static_assert(std::is_move_constructible_v<Keylet>);
static_assert(std::is_copy_assignable_v<Keylet>);
static_assert(std::is_move_assignable_v<Keylet>);
static_assert(std::is_nothrow_destructible_v<Keylet>);
#endif

template <bool>
class AcctRootImpl;

struct AccountRootKeylet final : public KeyletBase
{
    template <bool Writable>
    using TWrapped = AcctRootImpl<Writable>;

    using KeyletBase::check;

    AccountRootKeylet(uint256 const& key) : KeyletBase(ltACCOUNT_ROOT, key)
    {
    }
};

#ifndef __INTELLISENSE__
static_assert(!std::is_default_constructible_v<AccountRootKeylet>);
static_assert(std::is_copy_constructible_v<AccountRootKeylet>);
static_assert(std::is_move_constructible_v<AccountRootKeylet>);
static_assert(std::is_copy_assignable_v<AccountRootKeylet>);
static_assert(std::is_move_assignable_v<AccountRootKeylet>);
static_assert(std::is_nothrow_destructible_v<AccountRootKeylet>);
#endif

}  // namespace ripple

#endif
