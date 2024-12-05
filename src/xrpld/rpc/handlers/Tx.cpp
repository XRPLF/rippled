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

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/rpc/CTID.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/basics/ToString.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <charconv>
#include <regex>

namespace ripple {

static bool
isValidated(LedgerMaster& ledgerMaster, std::uint32_t seq, uint256 const& hash)
{
    if (!ledgerMaster.haveLedger(seq))
        return false;

    if (seq > ledgerMaster.getValidatedLedger()->info().seq)
        return false;

    return ledgerMaster.getHashBySeq(seq) == hash;
}

struct TxResult
{
    Transaction::pointer txn;
    std::variant<std::shared_ptr<TxMeta>, Blob> meta;
    bool validated = false;
    std::optional<std::string> ctid;
    std::optional<NetClock::time_point> closeTime;
    std::optional<uint256> ledgerHash;
    TxSearched searchedAll;
};

struct TxArgs
{
    std::optional<uint256> hash;
    std::optional<std::pair<uint32_t, uint16_t>> ctid;
    bool binary = false;
    std::optional<std::pair<uint32_t, uint32_t>> ledgerRange;
};

std::pair<TxResult, RPC::Status>
doTxHelp(RPC::Context& context, TxArgs args)
{
    TxResult result;

    ClosedInterval<uint32_t> range;

    if (args.ledgerRange)
    {
        constexpr uint16_t MAX_RANGE = 1000;

        if (args.ledgerRange->second < args.ledgerRange->first)
            return {result, rpcINVALID_LGR_RANGE};

        if (args.ledgerRange->second - args.ledgerRange->first > MAX_RANGE)
            return {result, rpcEXCESSIVE_LGR_RANGE};

        range = ClosedInterval<uint32_t>(
            args.ledgerRange->first, args.ledgerRange->second);
    }

    auto ec{rpcSUCCESS};

    using TxPair =
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;

    result.searchedAll = TxSearched::unknown;
    std::variant<TxPair, TxSearched> v;

    if (args.ctid)
    {
        args.hash = context.app.getLedgerMaster().txnIdFromIndex(
            args.ctid->first, args.ctid->second);

        if (args.hash)
            range =
                ClosedInterval<uint32_t>(args.ctid->first, args.ctid->second);
    }

    if (!args.hash)
        return {result, rpcTXN_NOT_FOUND};

    if (args.ledgerRange)
    {
        v = context.app.getMasterTransaction().fetch(*(args.hash), range, ec);
    }
    else
    {
        v = context.app.getMasterTransaction().fetch(*(args.hash), ec);
    }

    if (auto e = std::get_if<TxSearched>(&v))
    {
        result.searchedAll = *e;
        return {result, rpcTXN_NOT_FOUND};
    }

    auto [txn, meta] = std::get<TxPair>(v);

    if (ec == rpcDB_DESERIALIZATION)
    {
        return {result, ec};
    }
    if (!txn)
    {
        return {result, rpcTXN_NOT_FOUND};
    }

    // populate transaction data
    result.txn = txn;
    if (txn->getLedger() == 0)
    {
        return {result, rpcSUCCESS};
    }

    std::shared_ptr<Ledger const> ledger =
        context.ledgerMaster.getLedgerBySeq(txn->getLedger());

    if (ledger && !ledger->open())
        result.ledgerHash = ledger->info().hash;

    if (ledger && meta)
    {
        if (args.binary)
        {
            result.meta = meta->getAsObject().getSerializer().getData();
        }
        else
        {
            result.meta = meta;
        }
        result.validated = isValidated(
            context.ledgerMaster, ledger->info().seq, ledger->info().hash);
        if (result.validated)
            result.closeTime =
                context.ledgerMaster.getCloseTimeBySeq(txn->getLedger());

        // compute outgoing CTID
        uint32_t lgrSeq = ledger->info().seq;
        uint32_t txnIdx = meta->getAsObject().getFieldU32(sfTransactionIndex);
        uint32_t netID = context.app.config().NETWORK_ID;

        if (txnIdx <= 0xFFFFU && netID < 0xFFFFU && lgrSeq < 0x0FFF'FFFFUL)
            result.ctid =
                RPC::encodeCTID(lgrSeq, (uint16_t)txnIdx, (uint16_t)netID);
    }

    return {result, rpcSUCCESS};
}

Json::Value
populateJsonResponse(
    std::pair<TxResult, RPC::Status> const& res,
    TxArgs const& args,
    RPC::JsonContext const& context)
{
    Json::Value response;
    RPC::Status const& error = res.second;
    TxResult const& result = res.first;
    // handle errors
    if (error.toErrorCode() != rpcSUCCESS)
    {
        if (error.toErrorCode() == rpcTXN_NOT_FOUND &&
            result.searchedAll != TxSearched::unknown)
        {
            response = Json::Value(Json::objectValue);
            response[jss::searched_all] =
                (result.searchedAll == TxSearched::all);
            error.inject(response);
        }
        else
        {
            error.inject(response);
        }
    }
    // no errors
    else if (result.txn)
    {
        auto const& sttx = result.txn->getSTransaction();
        if (context.apiVersion > 1)
        {
            constexpr auto optionsJson =
                JsonOptions::include_date | JsonOptions::disable_API_prior_V2;
            if (args.binary)
                response[jss::tx_blob] = result.txn->getJson(optionsJson, true);
            else
            {
                response[jss::tx_json] = result.txn->getJson(optionsJson);
                RPC::insertDeliverMax(
                    response[jss::tx_json],
                    sttx->getTxnType(),
                    context.apiVersion);
            }

            // Note, result.ledgerHash is only set in a closed or validated
            // ledger - as seen in `doTxHelp`
            if (result.ledgerHash)
                response[jss::ledger_hash] = to_string(*result.ledgerHash);

            response[jss::hash] = to_string(result.txn->getID());
            if (result.validated)
            {
                response[jss::ledger_index] = result.txn->getLedger();
                if (result.closeTime)
                    response[jss::close_time_iso] =
                        to_string_iso(*result.closeTime);
            }
        }
        else
        {
            response =
                result.txn->getJson(JsonOptions::include_date, args.binary);
            if (!args.binary)
                RPC::insertDeliverMax(
                    response, sttx->getTxnType(), context.apiVersion);
        }

        // populate binary metadata
        if (auto blob = std::get_if<Blob>(&result.meta))
        {
            ASSERT(args.binary, "ripple::populateJsonResponse : binary is set");
            auto json_meta =
                (context.apiVersion > 1 ? jss::meta_blob : jss::meta);
            response[json_meta] = strHex(makeSlice(*blob));
        }
        // populate meta data
        else if (auto m = std::get_if<std::shared_ptr<TxMeta>>(&result.meta))
        {
            auto& meta = *m;
            if (meta)
            {
                response[jss::meta] = meta->getJson(JsonOptions::none);
                insertDeliveredAmount(
                    response[jss::meta], context, result.txn, *meta);
                insertNFTSyntheticInJson(response, sttx, *meta);
                RPC::insertMPTokenIssuanceID(response[jss::meta], sttx, *meta);
            }
        }
        response[jss::validated] = result.validated;

        if (result.ctid)
            response[jss::ctid] = *(result.ctid);
    }
    return response;
}

Json::Value
doTxJson(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(rpcNOT_ENABLED);

    // Deserialize and validate JSON arguments

    TxArgs args;

    if (context.params.isMember(jss::transaction) &&
        context.params.isMember(jss::ctid))
        // specifying both is ambiguous
        return rpcError(rpcINVALID_PARAMS);

    if (context.params.isMember(jss::transaction))
    {
        uint256 hash;
        if (!hash.parseHex(context.params[jss::transaction].asString()))
            return rpcError(rpcNOT_IMPL);
        args.hash = hash;
    }
    else if (context.params.isMember(jss::ctid))
    {
        auto ctid = RPC::decodeCTID(context.params[jss::ctid].asString());
        if (!ctid)
            return rpcError(rpcINVALID_PARAMS);

        auto const [lgr_seq, txn_idx, net_id] = *ctid;
        if (net_id != context.app.config().NETWORK_ID)
        {
            std::stringstream out;
            out << "Wrong network. You should submit this request to a node "
                   "running on NetworkID: "
                << net_id;
            return RPC::make_error(rpcWRONG_NETWORK, out.str());
        }
        args.ctid = {lgr_seq, txn_idx};
    }
    else
        return rpcError(rpcINVALID_PARAMS);

    args.binary = context.params.isMember(jss::binary) &&
        context.params[jss::binary].asBool();

    if (context.params.isMember(jss::min_ledger) &&
        context.params.isMember(jss::max_ledger))
    {
        try
        {
            args.ledgerRange = std::make_pair(
                context.params[jss::min_ledger].asUInt(),
                context.params[jss::max_ledger].asUInt());
        }
        catch (...)
        {
            // One of the calls to `asUInt ()` failed.
            return rpcError(rpcINVALID_LGR_RANGE);
        }
    }

    std::pair<TxResult, RPC::Status> res = doTxHelp(context, args);
    return populateJsonResponse(res, args, context);
}

}  // namespace ripple
