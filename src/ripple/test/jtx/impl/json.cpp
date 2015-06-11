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
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/test/jtx/json.h>

namespace ripple {
namespace test {
namespace jtx {

Json::Value
fset (Account const& account,
    std::uint32_t on, std::uint32_t off)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "AccountSet";
    if (on != 0)
        jv[jss::SetFlag] = on;
    if (off != 0)
        jv[jss::ClearFlag] = off;
    return jv;
}

Json::Value
pay (Account const& account,
    Account const& to,
        AnyAmount amount)
{
    amount.to(to);
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::Amount] = amount.value.getJson(0);
    jv[jss::Destination] = to.human();
    jv[jss::TransactionType] = "Payment";
    jv[jss::Flags] = tfUniversal;
    return jv;
}

Json::Value
offer (Account const& account,
    STAmount const& in, STAmount const& out)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TakerPays] = in.getJson(0);
    jv[jss::TakerGets] = out.getJson(0);
    jv[jss::TransactionType] = "OfferCreate";
    return jv;
}

Json::Value
rate (Account const& account, double multiplier)
{
    if (multiplier > 4)
        throw std::runtime_error(
            "rate multiplier out of range");
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransferRate] = std::uint32_t(
        1000000000 * multiplier);
    jv[jss::TransactionType] = "AccountSet";
    return jv;
}

Json::Value
regkey (Account const& account,
    disabled_t)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "SetRegularKey";
    return jv;
}

Json::Value
regkey (Account const& account,
    Account const& signer)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv["RegularKey"] = to_string(signer.id());
    jv[jss::TransactionType] = "SetRegularKey";
    return jv;
}

Json::Value
trust (Account const& account,
    STAmount const& amount)
{
    if (isXRP(amount))
        throw std::runtime_error(
            "trust() requires IOU");
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::LimitAmount] = amount.getJson(0);
    jv[jss::TransactionType] = "TrustSet";
    jv[jss::Flags] = 0;    // tfClearNoRipple;
    return jv;
}

} // jtx
} // test
} // ripple
