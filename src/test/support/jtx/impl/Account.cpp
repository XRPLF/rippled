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
#include <test/support/jtx/Account.h>
#include <test/support/jtx/amount.h>
#include <ripple/protocol/types.h>

namespace ripple {
namespace test {
namespace jtx {

std::unordered_map<
    std::pair<std::string, KeyType>,
        Account, beast::uhash<>> Account::cache_;

Account const Account::master("master",
    generateKeyPair(KeyType::secp256k1,
        generateSeed("masterpassphrase")), Account::privateCtorTag{});

Account::Account(std::string name,
        std::pair<PublicKey, SecretKey> const& keys, Account::privateCtorTag)
    : name_(std::move(name))
    , pk_ (keys.first)
    , sk_ (keys.second)
    , id_ (calcAccountID(pk_))
    , human_ (toBase58(id_))
{
}

Account Account::fromCache(std::string name, KeyType type)
{
    auto const p = std::make_pair (name, type);
    auto const iter = cache_.find (p);
    if (iter != cache_.end ())
        return iter->second;

    auto const keys = generateKeyPair (type, generateSeed (name));
    auto r = cache_.emplace (std::piecewise_construct,
        std::forward_as_tuple (std::move (p)),
        std::forward_as_tuple (std::move (name), keys, privateCtorTag{}));
    return r.first->second;
}

Account::Account (std::string name, KeyType type)
    : Account (fromCache (std::move (name), type))
{
}

IOU
Account::operator[](std::string const& s) const
{
    auto const currency = to_currency(s);
    assert(currency != noCurrency());
    return IOU(*this, currency);
}

} // jtx
} // test
} // ripple
