//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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
#include <ripple/rpc/impl/AccountFromString.h>
#include <ripple/rpc/impl/FieldReader.h>
#include <ripple/rpc/impl/LookupLedger.h>
#include <ripple/app/paths/RippleState.h>

namespace ripple {

/**
    Query:
    1) Specify ledger to query.

    2) Specify issuer account (cold wallet) in "account" field.

    3) Specify accounts that hold gateway assets (such as hot wallets)
       using "hotwallet" field which should be either a string (if just
       one wallet) or an array of strings (if more than one).

    Response:
    A Json object with three fields:

    1) "obligations", an array indicating the total obligations of the
       gateway in each currency. Obligations to specified hot wallets
       are not counted here.

    2) "balances", an object indicating balances in each account
       that holds gateway assets. (Those specified in the "hotwallet"
       field.)

    3) "assets", an object indicating accounts that owe the gateway.
       (Gateways typically do not hold positive balances. This is unusual.)
*/

// gateway_balances [<ledger>] <account> [<howallet> [<hotwallet [...

Json::Value doGatewayBalances (RPC::Context& context)
{
    Ledger::pointer ledger;
    std::set <Account> hotWallets;
    RippleAddress naAccount;

    {
        RPC::FieldReader reader {context};
        auto success =
                readLedger (reader, ledger) &&
                readRequired (reader, hotWallets, jss::hotwallet) &&
                readAccountAddress (reader, naAccount);

        if (! success)
            return reader.error;
    }

    context.loadType = Resource::feeHighBurdenRPC;

    Json::Value result;
    std::map <Currency, STAmount> obligations;

    using AccountBalances = std::map <Account, std::vector <STAmount>>;
    AccountBalances hotBalances, assets;

    result[jss::account] = naAccount.humanAccountID();
    auto accountID = naAccount.getAccountID();

    // Traverse the cold wallet's trust lines.
    forEachItem (*ledger, accountID, getApp().getSLECache(),
                 [&] (std::shared_ptr<SLE const> const& sle)
    {
        if (sle->getType() != ltRIPPLE_STATE)
            return;

        auto rs = RippleState::makeItem (accountID, sle);
        auto balance = rs->getBalance ();
        if (! balance)
            return;

        // Get the counterparty for the trustline.
        auto const& peer = rs->getAccountIDPeer();

        // A negative balance means the cold wallet owes (normal).
        // A positive balance means the cold wallet has an asset (unusual).

        if (hotWallets.count (peer) > 0)  // This is a specified hot wallet.
            hotBalances[peer].push_back (- balance);

        else if (balance > zero)          // This is a gateway asset.
            assets[peer].push_back (balance);

        else   // Normal negative balance: an obligation to a customer.
        {
            auto& o = obligations[balance.getCurrency()];
            if (o == zero)
                o = - balance;  // o might not have a currency code yet.
            else
                o -= balance;
            // TODO(tom): should this be the default behavior for
            // STAmount::operator+= and STAmount::operator-=?
        }
    });

    if (! obligations.empty())
    {
        Json::Value& j = result [jss::obligations];
        for (auto const& o : obligations)
            j[to_string (o.first)] = o.second.getText ();
    }

    auto balancesToJson = [&] (
        AccountBalances const& balances, Json::StaticString field)
    {
        if (balances.empty())
            return;

        auto& jsonBalances = result[field];
        for (auto const& account : balances)
        {
            auto& balanceArray = jsonBalances[to_string (account.first)];
            for (auto const& balance : account.second)
            {
                Json::Value& entry = balanceArray.append (Json::objectValue);
                entry[jss::currency] = to_string (balance.issue().currency);
                entry[jss::value] = balance.getText();
            }
        }
    };

    balancesToJson (hotBalances, jss::balances);
    balancesToJson (assets, jss::assets);

    return result;
}

} // ripple
