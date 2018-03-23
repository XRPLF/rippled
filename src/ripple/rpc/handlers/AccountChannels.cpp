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
#include <ripple/app/main/Application.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>
namespace ripple {

void addChannel (Json::Value& jsonLines, SLE const& line)
{
    Json::Value& jDst (jsonLines.append (Json::objectValue));
    jDst[jss::channel_id] = to_string (line.key ());
    jDst[jss::account] = to_string (line[sfAccount]);
    jDst[jss::destination_account] = to_string (line[sfDestination]);
    jDst[jss::amount] = line[sfAmount].getText ();
    jDst[jss::balance] = line[sfBalance].getText ();
    if (publicKeyType(line[sfPublicKey]))
    {
        PublicKey const pk (line[sfPublicKey]);
        jDst[jss::public_key] = toBase58 (TokenType::AccountPublic, pk);
        jDst[jss::public_key_hex] = strHex (pk);
    }
    jDst[jss::settle_delay] = line[sfSettleDelay];
    if (auto const& v = line[~sfExpiration])
        jDst[jss::expiration] = *v;
    if (auto const& v = line[~sfCancelAfter])
        jDst[jss::cancel_after] = *v;
    if (auto const& v = line[~sfSourceTag])
        jDst[jss::source_tag] = *v;
    if (auto const& v = line[~sfDestinationTag])
        jDst[jss::destination_tag] = *v;
}

// {
//   account: <account>|<account_public_key>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
Json::Value doAccountChannels (RPC::Context& context)
{
    auto const& params (context.params);
    if (! params.isMember (jss::account))
        return RPC::missing_field_error (jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger (ledger, context);
    if (! ledger)
        return result;

    std::string strIdent (params[jss::account].asString ());
    AccountID accountID;

    if (auto const actResult = RPC::accountFromString (accountID, strIdent))
        return actResult;

    if (! ledger->exists(keylet::account (accountID)))
        return rpcError (rpcACT_NOT_FOUND);

    std::string strDst;
    if (params.isMember (jss::destination_account))
        strDst = params[jss::destination_account].asString ();
    auto hasDst = ! strDst.empty ();

    AccountID raDstAccount;
    if (hasDst)
    {
        if (auto const actResult = RPC::accountFromString (raDstAccount, strDst))
            return actResult;
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountChannels, context))
        return *err;

    Json::Value jsonChannels{Json::arrayValue};
    struct VisitData
    {
        std::vector <std::shared_ptr<SLE const>> items;
        AccountID const& accountID;
        bool hasDst;
        AccountID const& raDstAccount;
    };
    VisitData visitData = {{}, accountID, hasDst, raDstAccount};
    unsigned int reserve (limit);
    uint256 startAfter;
    std::uint64_t startHint;

    if (params.isMember (jss::marker))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        Json::Value const& marker (params[jss::marker]);

        if (! marker.isString ())
            return RPC::expected_field_error (jss::marker, "string");

        startAfter.SetHex (marker.asString ());
        auto const sleChannel = ledger->read({ltPAYCHAN, startAfter});

        if (! sleChannel)
            return rpcError (rpcINVALID_PARAMS);

        if (sleChannel->getFieldAmount (sfLowLimit).getIssuer () == accountID)
            startHint = sleChannel->getFieldU64 (sfLowNode);
        else if (sleChannel->getFieldAmount (sfHighLimit).getIssuer () == accountID)
            startHint = sleChannel->getFieldU64 (sfHighNode);
        else
            return rpcError (rpcINVALID_PARAMS);

        addChannel (jsonChannels, *sleChannel);
        visitData.items.reserve (reserve);
    }
    else
    {
        startHint = 0;
        // We have no start point, limit should be one higher than requested.
        visitData.items.reserve (++reserve);
    }

    if (! forEachItemAfter(*ledger, accountID,
            startAfter, startHint, reserve,
        [&visitData](std::shared_ptr<SLE const> const& sleCur)
        {

            if (sleCur && sleCur->getType () == ltPAYCHAN &&
                (! visitData.hasDst ||
                 visitData.raDstAccount == (*sleCur)[sfDestination]))
            {
                visitData.items.emplace_back (sleCur);
                return true;
            }

            return false;
        }))
    {
        return rpcError (rpcINVALID_PARAMS);
    }

    if (visitData.items.size () == reserve)
    {
        result[jss::limit] = limit;

        result[jss::marker] = to_string (visitData.items.back()->key());
        visitData.items.pop_back ();
    }

    result[jss::account] = context.app.accountIDCache().toBase58 (accountID);

    for (auto const& item : visitData.items)
        addChannel (jsonChannels, *item);

    context.loadType = Resource::feeMediumBurdenRPC;
    result[jss::channels] = std::move(jsonChannels);
    return result;
}

} // ripple
