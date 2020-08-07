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

#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/tokens.h>
#include <cstring>

namespace ripple {

std::string
toBase58(AccountID const& v)
{
    return encodeBase58Token(TokenType::AccountID, v.data(), v.size());
}

template <>
boost::optional<AccountID>
parseBase58(std::string const& s)
{
    auto const result = decodeBase58Token(s, TokenType::AccountID);
    if (result.empty())
        return boost::none;
    AccountID id;
    if (result.size() != id.size())
        return boost::none;
    std::memcpy(id.data(), result.data(), result.size());
    return id;
}

bool
deprecatedParseBase58(AccountID& account, Json::Value const& jv)
{
    if (!jv.isString())
        return false;
    auto const result = parseBase58<AccountID>(jv.asString());
    if (!result)
        return false;
    account = *result;
    return true;
}

template <>
boost::optional<AccountID>
parseHex(std::string const& s)
{
    if (s.size() != 40)
        return boost::none;
    AccountID id;
    if (!id.SetHex(s, true))
        return boost::none;
    return id;
}

template <>
boost::optional<AccountID>
parseHexOrBase58(std::string const& s)
{
    auto result = parseHex<AccountID>(s);
    if (!result)
        result = parseBase58<AccountID>(s);
    return result;
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
    ripesha_hasher rsh;
    rsh(pk.data(), pk.size());
    auto const d = static_cast<ripesha_hasher::result_type>(rsh);
    AccountID id;
    static_assert(sizeof(d) == id.size(), "");
    std::memcpy(id.data(), d.data(), d.size());
    return id;
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
    if (s.size() == (160 / 4))
    {
        issuer.SetHex(s);
        return true;
    }
    auto const account = parseBase58<AccountID>(s);
    if (!account)
        return false;
    issuer = *account;
    return true;
}

//------------------------------------------------------------------------------

/*  VFALCO NOTE
    An alternate implementation could use a pair of insert-only
    hash maps that each use a single large memory allocation
    to store a fixed size hash table and all of the AccountID/string
    pairs laid out in memory (wouldn't use std::string here just a
    length prefixed or zero terminated array). Possibly using
    boost::intrusive as the basis for the unordered container.
    This would cut down to one allocate/free cycle per swap of
    the map.
*/

AccountIDCache::AccountIDCache(std::size_t capacity) : capacity_(capacity)
{
    m1_.reserve(capacity_);
}

std::string
AccountIDCache::toBase58(AccountID const& id) const
{
    std::lock_guard lock(mutex_);
    auto iter = m1_.find(id);
    if (iter != m1_.end())
        return iter->second;
    iter = m0_.find(id);
    std::string result;
    if (iter != m0_.end())
    {
        result = iter->second;
        // Can use insert-only hash maps if
        // we didn't erase from here.
        m0_.erase(iter);
    }
    else
    {
        result = ripple::toBase58(id);
    }
    if (m1_.size() >= capacity_)
    {
        m0_ = std::move(m1_);
        m1_.clear();
        m1_.reserve(capacity_);
    }
    m1_.emplace(id, result);
    return result;
}

}  // namespace ripple
