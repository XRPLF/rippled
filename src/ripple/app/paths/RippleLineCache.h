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

#ifndef RIPPLE_RIPPLELINECACHE_H
#define RIPPLE_RIPPLELINECACHE_H

#include <ripple/app/paths/RippleState.h>
#include <cstddef>
#include <memory>
#include <vector>

namespace ripple {

// Used by Pathfinder
class RippleLineCache
{
public:
    typedef std::vector <RippleState::pointer> RippleStateVector;
    typedef std::shared_ptr <RippleLineCache> pointer;
    typedef pointer const& ref;

    explicit RippleLineCache (Ledger::ref l);

    Ledger::ref getLedger () // VFALCO TODO const?
    {
        return mLedger;
    }

    std::vector<RippleState::pointer> const&
    getRippleLines (Account const& accountID);

private:
    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    Ledger::pointer mLedger;

    struct AccountKey
    {
        Account account_;
        std::size_t hash_value_;

        AccountKey (Account const& account)
            : account_ (account)
            , hash_value_ (beast::hardened_hash<>{}(account))
        {

        }

        AccountKey (AccountKey const& other)
            : account_ (other.account_)
            , hash_value_ (other.hash_value_)
        {

        }

        AccountKey& operator=(AccountKey const& other)
        {
            account_ = other.account_;
            hash_value_ = other.hash_value_;
            return *this;
        }

        bool operator== (AccountKey const& lhs) const
        {
            return hash_value_ == lhs.hash_value_ &&
                account_ == lhs.account_;
        }

        std::size_t
        get_hash () const
        {
            return hash_value_;
        }

        struct Hash
        {
            std::size_t
            operator () (AccountKey const& key) const noexcept
            {
                return key.get_hash ();
            }
        };
    };

    hash_map <AccountKey, RippleStateVector, AccountKey::Hash> mRLMap;
};

} // ripple

#endif
