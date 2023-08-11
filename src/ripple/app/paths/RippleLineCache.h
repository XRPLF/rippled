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

#ifndef RIPPLE_APP_PATHS_RIPPLELINECACHE_H_INCLUDED
#define RIPPLE_APP_PATHS_RIPPLELINECACHE_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/paths/TrustLine.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/hardened_hash.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace ripple {

// Used by Pathfinder
class RippleLineCache final : public CountedObject<RippleLineCache>
{
public:
    explicit RippleLineCache(
        std::shared_ptr<ReadView const> const& l,
        beast::Journal j);
    ~RippleLineCache();

    std::shared_ptr<ReadView const> const&
    getLedger() const
    {
        return ledger_;
    }

    /** Find the trust lines associated with an account.

       @param accountID The account
       @param direction Whether the account is an "outgoing" link on the path.
       "Outgoing" is defined as the source account, or an account found via a
       trustline that has rippling enabled on the @accountID's side. If an
       account is "outgoing", all trust lines will be returned. If an account is
       not "outgoing", then any trust lines that don't have rippling enabled are
       not usable, so only return trust lines that have rippling enabled on
       @accountID's side.
       @return Returns a vector of the usable trust lines.
    */
    std::shared_ptr<std::vector<PathFindTrustLine>>
    getRippleLines(AccountID const& accountID, LineDirection direction);

private:
    std::mutex mLock;

    ripple::hardened_hash<> hasher_;
    std::shared_ptr<ReadView const> ledger_;

    beast::Journal journal_;

    struct AccountKey final : public CountedObject<AccountKey>
    {
        AccountID account_;
        LineDirection direction_;
        std::size_t hash_value_;

        AccountKey(
            AccountID const& account,
            LineDirection direction,
            std::size_t hash)
            : account_(account), direction_(direction), hash_value_(hash)
        {
        }

        AccountKey(AccountKey const& other) = default;

        AccountKey&
        operator=(AccountKey const& other) = default;

        bool
        operator==(AccountKey const& lhs) const
        {
            return hash_value_ == lhs.hash_value_ && account_ == lhs.account_ &&
                direction_ == lhs.direction_;
        }

        std::size_t
        get_hash() const
        {
            return hash_value_;
        }

        struct Hash
        {
            explicit Hash() = default;

            std::size_t
            operator()(AccountKey const& key) const noexcept
            {
                return key.get_hash();
            }
        };
    };

    // Use a shared_ptr so entries can be removed from the map safely.
    // Even though a shared_ptr to a vector will take more memory just a vector,
    // most accounts are not going to have any entries (estimated over 90%), so
    // vectors will not need to be created for them. This should lead to far
    // less memory usage overall.
    hash_map<
        AccountKey,
        std::shared_ptr<std::vector<PathFindTrustLine>>,
        AccountKey::Hash>
        lines_;
    std::size_t totalLineCount_ = 0;
};

}  // namespace ripple

#endif
