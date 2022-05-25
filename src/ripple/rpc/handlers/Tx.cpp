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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/basics/ToString.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// {
//   transaction: <hex>
// }

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
    TxSearched searchedAll;
};

struct TxArgs
{
    uint256 hash;
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

    JLOG(context.j.debug()) << "Fetching from postgres";
    Transaction::Locator locator = Transaction::locate(args.hash, context.app);

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
                    args.hash, res.txn->getLedger(), *meta);
            }
            res.validated = true;
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
doTxHelp(RPC::Context& context, TxArgs const& args)
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
    if (args.ledgerRange)
    {
        v = context.app.getMasterTransaction().fetch(args.hash, range, ec);
    }
    else
    {
        v = context.app.getMasterTransaction().fetch(args.hash, ec);
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
    }

    return {result, rpcSUCCESS};
}

std::pair<org::xrpl::rpc::v1::GetTransactionResponse, grpc::Status>
populateProtoResponse(
    std::pair<TxResult, RPC::Status> const& res,
    TxArgs const& args,
    RPC::GRPCContext<org::xrpl::rpc::v1::GetTransactionRequest> const& context)
{
    org::xrpl::rpc::v1::GetTransactionResponse response;
    grpc::Status status = grpc::Status::OK;
    RPC::Status const& error = res.second;
    TxResult const& result = res.first;
    // handle errors
    if (error.toErrorCode() != rpcSUCCESS)
    {
        if (error.toErrorCode() == rpcTXN_NOT_FOUND &&
            result.searchedAll != TxSearched::unknown)
        {
            status = {
                grpc::StatusCode::NOT_FOUND,
                "txn not found. searched_all = " +
                    to_string(
                        (result.searchedAll == TxSearched::all ? "true"
                                                               : "false"))};
        }
        else
        {
            if (error.toErrorCode() == rpcTXN_NOT_FOUND)
                status = {grpc::StatusCode::NOT_FOUND, "txn not found"};
            else
                status = {grpc::StatusCode::INTERNAL, error.message()};
        }
    }
    // no errors
    else if (result.txn)
    {
        auto& txn = result.txn;

        std::shared_ptr<STTx const> stTxn = txn->getSTransaction();
        if (args.binary)
        {
            Serializer s = stTxn->getSerializer();
            response.set_transaction_binary(s.data(), s.size());
        }
        else
        {
            RPC::convert(*response.mutable_transaction(), stTxn);
        }

        response.set_hash(context.params.hash());

        auto ledgerIndex = txn->getLedger();
        response.set_ledger_index(ledgerIndex);
        if (ledgerIndex)
        {
            auto ct =
                context.app.getLedgerMaster().getCloseTimeBySeq(ledgerIndex);
            if (ct)
                response.mutable_date()->set_value(
                    ct->time_since_epoch().count());
        }

        RPC::convert(
            *response.mutable_meta()->mutable_transaction_result(),
            txn->getResult());
        response.mutable_meta()->mutable_transaction_result()->set_result(
            transToken(txn->getResult()));

        // populate binary metadata
        if (auto blob = std::get_if<Blob>(&result.meta))
        {
            assert(args.binary);
            Slice slice = makeSlice(*blob);
            response.set_meta_binary(slice.data(), slice.size());
        }
        // populate meta data
        else if (auto m = std::get_if<std::shared_ptr<TxMeta>>(&result.meta))
        {
            auto& meta = *m;
            if (meta)
            {
                RPC::convert(*response.mutable_meta(), meta);
                auto amt =
                    getDeliveredAmount(context, stTxn, *meta, txn->getLedger());
                if (amt)
                {
                    RPC::convert(
                        *response.mutable_meta()->mutable_delivered_amount(),
                        *amt);
                }
            }
        }
        response.set_validated(result.validated);
    }
    return {response, status};
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
        response = result.txn->getJson(JsonOptions::include_date, args.binary);

        // populate binary metadata
        if (auto blob = std::get_if<Blob>(&result.meta))
        {
            assert(args.binary);
            response[jss::meta] = strHex(makeSlice(*blob));
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
            }
        }
        response[jss::validated] = result.validated;
    }
    return response;
}

Json::Value
doTxJson(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(rpcNOT_ENABLED);

    // Deserialize and validate JSON arguments

    if (!context.params.isMember(jss::transaction))
        return rpcError(rpcINVALID_PARAMS);

    TxArgs args;

    if (!args.hash.parseHex(context.params[jss::transaction].asString()))
        return rpcError(rpcNOT_IMPL);

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

std::pair<org::xrpl::rpc::v1::GetTransactionResponse, grpc::Status>
doTxGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetTransactionRequest>& context)
{
    if (!context.app.config().useTxTables())
    {
        return {
            {},
            {grpc::StatusCode::UNIMPLEMENTED, "Not enabled in configuration."}};
    }

    // return values
    org::xrpl::rpc::v1::GetTransactionResponse response;
    grpc::Status status = grpc::Status::OK;

    // input
    org::xrpl::rpc::v1::GetTransactionRequest& request = context.params;

    TxArgs args;

    if (auto hash = uint256::fromVoidChecked(request.hash()))
    {
        args.hash = *hash;
    }
    else
    {
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "tx hash malformed"};
        return {response, errorStatus};
    }

    args.binary = request.binary();

    if (request.ledger_range().ledger_index_min() != 0 &&
        request.ledger_range().ledger_index_max() != 0)
    {
        args.ledgerRange = std::make_pair(
            request.ledger_range().ledger_index_min(),
            request.ledger_range().ledger_index_max());
    }

    std::pair<TxResult, RPC::Status> res = doTxHelp(context, args);
    return populateProtoResponse(res, args, context);
}

}  // namespace ripple
