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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/paths/TrustLine.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

static void
fillTransaction(
    RPC::JsonContext& context,
    Json::Value& txArray,
    AccountID const& accountID,
    std::uint32_t& sequence,
    ReadView const& ledger)
{
    txArray["Sequence"] = Json::UInt(sequence++);
    txArray["Account"] = toBase58(accountID);
    auto& fees = ledger.fees();
    // Convert the reference transaction cost in fee units to drops
    // scaled to represent the current fee load.
    txArray["Fee"] =
        scaleFeeLoad(fees.base, context.app.getFeeTrack(), fees, false)
            .jsonClipped();
}

// {
//   account: <account>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional, number of problems
//   role: gateway|user             // account role to assume
//   transactions: true             // optional, reccommend transactions
// }
Json::Value
doNoRippleCheck(RPC::JsonContext& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::account))
        return RPC::missing_field_error("account");

    if (!params.isMember("role"))
        return RPC::missing_field_error("role");

    if (!params[jss::account].isString())
        return RPC::invalid_field_error(jss::account);

    bool roleGateway = false;
    {
        std::string const role = params["role"].asString();
        if (role == "gateway")
            roleGateway = true;
        else if (role != "user")
            return RPC::invalid_field_error("role");
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::noRippleCheck, context))
        return *err;

    bool transactions = false;
    if (params.isMember(jss::transactions))
        transactions = params["transactions"].asBool();

    // The document[https://xrpl.org/noripple_check.html#noripple_check] states
    // that transactions params is a boolean value, however, assigning any
    // string value works. Do not allow this. This check is for api Version 2
    // onwards only
    if (context.apiVersion > 1u && params.isMember(jss::transactions) &&
        !params[jss::transactions].isBool())
    {
        return RPC::invalid_field_error(jss::transactions);
    }

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    Json::Value dummy;
    Json::Value& jvTransactions =
        transactions ? (result[jss::transactions] = Json::arrayValue) : dummy;

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto const accountID{std::move(id.value())};
    auto const sle = ledger->read(keylet::account(accountID));
    if (!sle)
        return rpcError(rpcACT_NOT_FOUND);

    std::uint32_t seq = sle->getFieldU32(sfSequence);

    Json::Value& problems = (result["problems"] = Json::arrayValue);

    bool bDefaultRipple = sle->getFieldU32(sfFlags) & lsfDefaultRipple;

    if (bDefaultRipple & !roleGateway)
    {
        problems.append(
            "You appear to have set your default ripple flag even though you "
            "are not a gateway. This is not recommended unless you are "
            "experimenting");
    }
    else if (roleGateway & !bDefaultRipple)
    {
        problems.append("You should immediately set your default ripple flag");
        if (transactions)
        {
            Json::Value& tx = jvTransactions.append(Json::objectValue);
            tx["TransactionType"] = jss::AccountSet;
            tx["SetFlag"] = 8;
            fillTransaction(context, tx, accountID, seq, *ledger);
        }
    }

    forEachItemAfter(
        *ledger,
        accountID,
        uint256(),
        0,
        limit,
        [&](std::shared_ptr<SLE const> const& ownedItem) {
            if (ownedItem->getType() == ltRIPPLE_STATE)
            {
                bool const bLow = accountID ==
                    ownedItem->getFieldAmount(sfLowLimit).getIssuer();

                bool const bNoRipple = ownedItem->getFieldU32(sfFlags) &
                    (bLow ? lsfLowNoRipple : lsfHighNoRipple);

                std::string problem;
                bool needFix = false;
                if (bNoRipple & roleGateway)
                {
                    problem = "You should clear the no ripple flag on your ";
                    needFix = true;
                }
                else if (!roleGateway & !bNoRipple)
                {
                    problem =
                        "You should probably set the no ripple flag on your ";
                    needFix = true;
                }
                if (needFix)
                {
                    AccountID peer =
                        ownedItem
                            ->getFieldAmount(bLow ? sfHighLimit : sfLowLimit)
                            .getIssuer();
                    STAmount peerLimit = ownedItem->getFieldAmount(
                        bLow ? sfHighLimit : sfLowLimit);
                    problem += to_string(peerLimit.getCurrency());
                    problem += " line to ";
                    problem += to_string(peerLimit.getIssuer());
                    problems.append(problem);

                    STAmount limitAmount(ownedItem->getFieldAmount(
                        bLow ? sfLowLimit : sfHighLimit));
                    limitAmount.setIssuer(peer);

                    Json::Value& tx = jvTransactions.append(Json::objectValue);
                    tx["TransactionType"] = jss::TrustSet;
                    tx["LimitAmount"] = limitAmount.getJson(JsonOptions::none);
                    tx["Flags"] = bNoRipple ? tfClearNoRipple : tfSetNoRipple;
                    fillTransaction(context, tx, accountID, seq, *ledger);

                    return true;
                }
            }
            return false;
        });

    return result;
}

}  // namespace ripple
