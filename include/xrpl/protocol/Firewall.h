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

#ifndef RIPPLE_PROTOCOL_FIREWALL_H_INCLUDED
#define RIPPLE_PROTOCOL_FIREWALL_H_INCLUDED

#include <xrpl/basics/XRPAmount.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>

namespace ripple {

inline void
serializeFirewallAuthorization(Serializer& msg, Json::Value const& authAccounts)
{
    msg.add32(HashPrefix::shardInfo);
    for (auto const& authAccount : authAccounts)
    {
        auto const account = authAccount[jss::AuthAccount];
        std::optional<AccountID> accountId =
            parseBase58<AccountID>(account[jss::Account].asString());
        msg.addBitString(*accountId);
        if (account[jss::Amount].isString())
        {
            std::cout << "isXRP" << "\n";
            std::optional<std::uint64_t> const optDrops =
                to_uint64(account[jss::Amount].asString());
            if (!optDrops)
            {
                // pass
            }
            else
            {
                msg.add64(XRPAmount(*optDrops).drops());
            }
        }
        else
        {
            std::cout << "isIOU" << "\n";
            STAmount amt;
            bool isAmount = amountFromJsonNoThrow(amt, account[jss::Amount]);
            if (!isAmount)
            {
                // pass
            }
            else
            {
                if (amt == beast::zero)
                    msg.add64(STAmount::cNotNative);
                else if (amt.signum() == -1)  // Negative amount
                    msg.add64(
                        amt.mantissa() |
                        (static_cast<std::uint64_t>(amt.exponent() + 512 + 97)
                         << (64 - 10)));
                else  // Positive amount
                    msg.add64(
                        amt.mantissa() |
                        (static_cast<std::uint64_t>(
                             amt.exponent() + 512 + 256 + 97)
                         << (64 - 10)));
                msg.addBitString(amt.getCurrency());
                msg.addBitString(amt.getIssuer());
            }
        }
    }
}

inline void
serializeFirewallAuthorization(Serializer& msg, STArray const& authAccounts)
{
    msg.add32(HashPrefix::shardInfo);
    for (auto const& authAccount : authAccounts)
    {
        AccountID accountId = authAccount.getAccountID(sfAccount);
        msg.addBitString(accountId);
        STAmount amt = authAccount.getFieldAmount(sfAmount);
        ;
        if (isXRP(amt))
        {
            msg.add64(amt.mantissa());
        }
        else
        {
            if (amt == beast::zero)
                msg.add64(STAmount::cNotNative);
            else if (amt.signum() == -1)  // Negative amount
                msg.add64(
                    amt.mantissa() |
                    (static_cast<std::uint64_t>(amt.exponent() + 512 + 97)
                     << (64 - 10)));
            else  // Positive amount
                msg.add64(
                    amt.mantissa() |
                    (static_cast<std::uint64_t>(amt.exponent() + 512 + 256 + 97)
                     << (64 - 10)));
            msg.addBitString(amt.getCurrency());
            msg.addBitString(amt.getIssuer());
        }
    }
}

}  // namespace ripple

#endif
