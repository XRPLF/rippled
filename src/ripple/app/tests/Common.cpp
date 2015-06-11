//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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
#include <ripple/app/tests/Common.h>
#include <ripple/protocol/SystemParameters.h>

namespace ripple {
namespace test {

namespace detail {

STAmount
XRP_t::operator()(double v) const
{
    if (v < 0)
        return STAmount(std::uint64_t(
            -v * SYSTEM_CURRENCY_PARTS), true);
    return STAmount(std::uint64_t(
        v * SYSTEM_CURRENCY_PARTS), false);
}

} // detail

detail::XRP_t XRP;

STAmount
IOU::operator()(double v) const
{
    return amountFromString(issue_,
        std::to_string(v));
}

STAmount
IOU::operator()(epsilon_t) const
{
    return STAmount(issue_, 1, -81);
}

STAmount
IOU::operator()(detail::epsilon_multiple m) const
{
    return STAmount(issue_, m.n, -81);
}

//------------------------------------------------------------------------------

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
    return IOU(Issue(currency, id()));
}

} // test
} // ripple
