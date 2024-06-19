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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/misc/DeliverMax.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/rdb/RelationalDatabase.h>
#include <ripple/basics/ToString.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/NFTSyntheticSerializer.h>
#include <ripple/protocol/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/CTID.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/RPCHelpers.h>

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
doTxPostgres(RPC::Context& context, TxArgs const& args)
{
    if (!context.app.config().reporting())
    {
        assert(false);
        Throw<std::runtime_error>(
            "Called doTxPostgres yet not in reporting mode");
    }

    TxResult res;
    res.searchedAll = TxSearched::unknown;

    if (!args.hash)
        return {
            res,
            {rpcNOT_IMPL,
             "Use of CTIDs on reporting mode is not currently supported."}};

    JLOG(context.j.debug()) << "Fetching from postgres";
    Transaction::Locator locator =
        Transaction::locate(*(args.hash), context.app);

    std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>
        pair;
    // database returned the nodestore hash. Fetch the txn directly from the
    // nodestore. Don't traverse the transaction SHAMap
    if (locator.isFound())
    {
        auto start = std::chrono::system_clock::now();
        // The second argument of fetch is ignored when not using shards
        if (auto obj = context.app.getNodeFamily().db().fetchNodeObject(
                locator.getNodestoreHash(), locator.getLedgerSequence()))
        {
            auto node = SHAMapTreeNode::makeFromPrefix(
                makeSlice(obj->getData()),
                SHAMapHash{locator.getNodestoreHash()});
            if (!node)
            {
                assert(false);
                return {res, {rpcINTERNAL, "Error making SHAMap node"}};
            }
            auto item = (static_cast<SHAMapLeafNode*>(node.get()))->peekItem();
            if (!item)
            {
                assert(false);
                return {res, {rpcINTERNAL, "Error reading SHAMap node"}};
            }

            auto [sttx, meta] = deserializeTxPlusMeta(*item);
            JLOG(context.j.debug()) << "Successfully fetched from db";

            if (!sttx || !meta)
            {
                assert(false);
                return {res, {rpcINTERNAL, "Error deserializing SHAMap node"}};
            }
            std::string reason;
            res.txn = std::make_shared<Transaction>(sttx, reason, context.app);
            res.txn->setLedger(locator.getLedgerSequence());
            res.txn->setStatus(COMMITTED);
            if (args.binary)
            {
                SerialIter it(item->slice());
                it.skip(it.getVLDataLength());  // skip transaction
                Blob blob = it.getVL();
                res.meta = std::move(blob);
            }
            else
            {
                res.meta = std::make_shared<TxMeta>(
                    *(args.hash), res.txn->getLedger(), *meta);
            }
            res.validated = true;

            auto const ledgerInfo =
                context.app.getRelationalDatabase().getLedgerInfoByIndex(
                    locator.getLedgerSequence());
            res.closeTime = ledgerInfo->closeTime;
            res.ledgerHash = ledgerInfo->hash;

            return {res, rpcSUCCESS};
        }
        else
        {
            JLOG(context.j.error()) << "Failed to fetch from db";
            assert(false);
            return {res, {rpcINTERNAL, "Containing SHAMap node not found"}};
        }
        auto end = std::chrono::system_clock::now();
        JLOG(context.j.debug()) << "tx flat fetch time : "
                                << ((end - start).count() / 1000000000.0);
    }
    // database did not find the transaction, and returned the ledger range
    // that was searched
    else
    {
        if (args.ledgerRange)
        {
            auto range = locator.getLedgerRangeSearched();
            auto min = args.ledgerRange->first;
            auto max = args.ledgerRange->second;
            if (min >= range.lower() && max <= range.upper())
            {
                res.searchedAll = TxSearched::all;
            }
            else
            {
                res.searchedAll = TxSearched::some;
            }
        }
        return {res, rpcTXN_NOT_FOUND};
    }
    // database didn't return anything. This shouldn't happen
    assert(false);
    return {res, {rpcINTERNAL, "unexpected Postgres response"}};
}

std::pair<TxResult, RPC::Status>
doTxHelp(RPC::Context& context, TxArgs args)
{
    if (context.app.config().reporting())
        return doTxPostgres(context, args);
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
        if (meta->getAsObject().isFieldPresent(sfTransactionIndex))
        {
            uint32_t lgrSeq = ledger->info().seq;
            uint32_t txnIdx =
                meta->getAsObject().getFieldU32(sfTransactionIndex);
            uint32_t netID = context.app.config().NETWORK_ID;

            if (txnIdx <= 0xFFFFU && netID < 0xFFFFU && lgrSeq < 0x0FFF'FFFFUL)
                result.ctid =
                    RPC::encodeCTID(lgrSeq, (uint32_t)txnIdx, (uint32_t)netID);
        }
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
            // ledger - as seen in `doTxHelp` and `doTxPostgres`
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
            assert(args.binary);
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
