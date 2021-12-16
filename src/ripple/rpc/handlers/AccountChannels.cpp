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

#include <ripple/app/main/Application.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>
namespace ripple {

void
addChannel(Json::Value& jsonLines, SLE const& line)
{
    Json::Value& jDst(jsonLines.append(Json::objectValue));
    jDst[jss::channel_id] = to_string(line.key());
    jDst[jss::account] = to_string(line[sfAccount]);
    jDst[jss::destination_account] = to_string(line[sfDestination]);
    jDst[jss::amount] = line[sfAmount].getText();
    jDst[jss::balance] = line[sfBalance].getText();
    if (publicKeyType(line[sfPublicKey]))
    {
        PublicKey const pk(line[sfPublicKey]);
        jDst[jss::public_key] = toBase58(TokenType::AccountPublic, pk);
        jDst[jss::public_key_hex] = strHex(pk);
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
Json::Value
doAccountChannels(RPC::JsonContext& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::account))
        return RPC::missing_field_error(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    std::string strIdent(params[jss::account].asString());
    AccountID accountID;

    if (auto const err = RPC::accountFromString(accountID, strIdent))
        return err;

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    std::string strDst;
    if (params.isMember(jss::destination_account))
        strDst = params[jss::destination_account].asString();
    auto hasDst = !strDst.empty();

    AccountID raDstAccount;
    if (hasDst)
    {
        if (auto const err = RPC::accountFromString(raDstAccount, strDst))
            return err;
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountChannels, context))
        return *err;

    if (limit == 0u)
        return rpcError(rpcINVALID_PARAMS);

    Json::Value jsonChannels{Json::arrayValue};
    struct VisitData
    {
        std::vector<std::shared_ptr<SLE const>> items;
        AccountID const& accountID;
        bool hasDst;
        AccountID const& raDstAccount;
    };
    VisitData visitData = {{}, accountID, hasDst, raDstAccount};
    visitData.items.reserve(limit);
    uint256 startAfter = beast::zero;
    std::uint64_t startHint = 0;

    if (params.isMember(jss::marker))
    {
        if (!params[jss::marker].isString())
            return RPC::expected_field_error(jss::marker, "string");

        // Marker is composed of a comma separated index and start hint. The
        // former will be read as hex, and the latter using boost lexical cast.
        std::stringstream marker(params[jss::marker].asString());
        std::string value;
        if (!std::getline(marker, value, ','))
            return rpcError(rpcINVALID_PARAMS);

        if (!startAfter.parseHex(value))
            return rpcError(rpcINVALID_PARAMS);

        if (!std::getline(marker, value, ','))
            return rpcError(rpcINVALID_PARAMS);

        try
        {
            startHint = boost::lexical_cast<std::uint64_t>(value);
        }
        catch (boost::bad_lexical_cast&)
        {
            return rpcError(rpcINVALID_PARAMS);
        }

        // We then must check if the object pointed to by the marker is actually
        // owned by the account in the request.
        auto const sle = ledger->read({ltANY, startAfter});

        if (!sle)
            return rpcError(rpcINVALID_PARAMS);

        if (!RPC::isOwnedByAccount(*ledger, sle, accountID))
            return rpcError(rpcINVALID_PARAMS);
    }

    auto count = 0;
    std::optional<uint256> marker = {};
    std::uint64_t nextHint = 0;
    if (!forEachItemAfter(
            *ledger,
            accountID,
            startAfter,
            startHint,
            limit + 1,
            [&visitData, &accountID, &count, &limit, &marker, &nextHint](
                std::shared_ptr<SLE const> const& sleCur) {
                if (!sleCur)
                {
                    assert(false);
                    return false;
                }

                if (++count == limit)
                {
                    marker = sleCur->key();
                    nextHint = RPC::getStartHint(sleCur, visitData.accountID);
                }

                if (count <= limit && sleCur->getType() == ltPAYCHAN &&
                    (*sleCur)[sfAccount] == accountID &&
                    (!visitData.hasDst ||
                     visitData.raDstAccount == (*sleCur)[sfDestination]))
                {
                    visitData.items.emplace_back(sleCur);
                }

                return true;
            }))
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    // Both conditions need to be checked because marker is set on the limit-th
    // item, but if there is no item on the limit + 1 iteration, then there is
    // no need to return a marker.
    if (count == limit + 1 && marker)
    {
        result[jss::limit] = limit;
        result[jss::marker] =
            to_string(*marker) + "," + std::to_string(nextHint);
    }

    result[jss::account] = context.app.accountIDCache().toBase58(accountID);

    for (auto const& item : visitData.items)
        addChannel(jsonChannels, *item);

    context.loadType = Resource::feeMediumBurdenRPC;
    result[jss::channels] = std::move(jsonChannels);
    return result;
}

}  // namespace ripple
