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
#include <ripple/test/jtx/Account.h>
#include <ripple/test/jtx/amount.h>

namespace ripple {
namespace test {
namespace jtx {

#ifdef _MSC_VER
Account::Account (Account&& other)
    : name_(std::move(other.name_))
    , pk_(std::move(other.pk_))
    , sk_(std::move(other.sk_))
    , id_(std::move(other.id_))
    , human_(std::move(other.human_))
{
}

Account&
Account::operator= (Account&& rhs)
{
    name_ = std::move(rhs.name_);
    pk_ = std::move(rhs.pk_);
    sk_ = std::move(rhs.sk_);
    id_ = std::move(rhs.id_);
    human_ = std::move(rhs.human_);
    return *this;
}
#endif

Account::Account(
        std::string name, KeyPair&& keys)
    : name_(std::move(name))
{
    pk_ = std::move(keys.publicKey);
    sk_ = std::move(keys.secretKey);
    id_ = pk_.getAccountID();
    human_ = pk_.humanAccountID();
}

Account::Account (std::string name,
        KeyType type)
#ifndef _MSC_VER
    : Account(name,
#else
    // Fails on Clang and possibly gcc
    : Account(std::move(name),
#endif
        generateKeysFromSeed(type,
            RippleAddress::createSeedGeneric(
                name)))
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
