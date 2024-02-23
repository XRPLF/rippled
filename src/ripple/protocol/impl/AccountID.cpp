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

#include <ripple/basics/hardened_hash.h>
#include <ripple/basics/spinlock.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/tokens.h>
#include <array>
#include <cstring>
#include <mutex>

namespace ripple {

namespace detail {

/** Caches the base58 representations of AccountIDs */
class AccountIdCache
{
private:
    struct CachedAccountID
    {
        AccountID id;
        char encoding[40] = {0};
    };

    // The actual cache
    std::vector<CachedAccountID> cache_;

    // We use a hash function designed to resist algorithmic complexity attacks
    hardened_hash<> hasher_;

    // 64 spinlocks, packed into a single 64-bit value
    std::atomic<std::uint64_t> locks_ = 0;

public:
    AccountIdCache(std::size_t count) : cache_(count)
    {
        // This is non-binding, but we try to avoid wasting memory that
        // is caused by overallocation.
        cache_.shrink_to_fit();
    }

    std::string
    toBase58(AccountID const& id)
    {
        auto const index = hasher_(id) % cache_.size();

        packed_spinlock sl(locks_, index % 64);

        {
            std::lock_guard lock(sl);

            // The check against the first character of the encoding ensures
            // that we don't mishandle the case of the all-zero account:
            if (cache_[index].encoding[0] != 0 && cache_[index].id == id)
                return cache_[index].encoding;
        }

        auto ret =
            encodeBase58Token(TokenType::AccountID, id.data(), id.size());

        assert(ret.size() <= 38);

        {
            std::lock_guard lock(sl);
            cache_[index].id = id;
            std::strcpy(cache_[index].encoding, ret.c_str());
        }

        return ret;
    }
};

}  // namespace detail

static std::unique_ptr<detail::AccountIdCache> accountIdCache;

void
initAccountIdCache(std::size_t count)
{
    if (!accountIdCache && count != 0)
        accountIdCache = std::make_unique<detail::AccountIdCache>(count);
}

std::string
toBase58(AccountID const& v)
{
    if (accountIdCache)
        return accountIdCache->toBase58(v);

    return encodeBase58Token(TokenType::AccountID, v.data(), v.size());
}

template <>
std::optional<AccountID>
parseBase58(std::string const& s)
{
    auto const result = decodeBase58Token(s, TokenType::AccountID);
    if (result.size() != AccountID::bytes)
        return std::nullopt;
    return AccountID{result};
}

//------------------------------------------------------------------------------
/*
    Calculation of the Account ID

    The AccountID is a 160-bit identifier that uniquely
    distinguishes an account. The account may or may not
    exist in the ledger. Even for accounts that are not in
    the ledger, cryptographic operations may be performed
    which affect the ledger. For example, designating an
    account not in the ledger as a regular key for an
    account that is in the ledger.

    Why did we use half of SHA512 for most things but then
    SHA256 followed by RIPEMD160 for account IDs? Why didn't
    we do SHA512 half then RIPEMD160? Or even SHA512 then RIPEMD160?
    For that matter why RIPEMD160 at all why not just SHA512 and keep
    only 160 bits?

    Answer (David Schwartz):

        The short answer is that we kept Bitcoin's behavior.
        The longer answer was that:
            1) Using a single hash could leave ripple
               vulnerable to length extension attacks.
            2) Only RIPEMD160 is generally considered safe at 160 bits.

        Any of those schemes would have been acceptable. However,
        the one chosen avoids any need to defend the scheme chosen.
        (Against any criticism other than unnecessary complexity.)

        "The historical reason was that in the very early days,
        we wanted to give people as few ways to argue that we were
        less secure than Bitcoin. So where there was no good reason
        to change something, it was not changed."
*/
AccountID
calcAccountID(PublicKey const& pk)
{
    static_assert(AccountID::bytes == sizeof(ripesha_hasher::result_type));

    ripesha_hasher rsh;
    rsh(pk.data(), pk.size());
    return AccountID{static_cast<ripesha_hasher::result_type>(rsh)};
}

AccountID const&
xrpAccount()
{
    static AccountID const account(beast::zero);
    return account;
}

AccountID const&
noAccount()
{
    static AccountID const account(1);
    return account;
}

bool
to_issuer(AccountID& issuer, std::string const& s)
{
    if (issuer.parseHex(s))
        return true;
    auto const account = parseBase58<AccountID>(s);
    if (!account)
        return false;
    issuer = *account;
    return true;
}

}  // namespace ripple
