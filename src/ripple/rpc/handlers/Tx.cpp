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
isHexTxID(std::string const& txid)
{
    if (txid.size() != 64)
        return false;

    auto const ret =
        std::find_if(txid.begin(), txid.end(), [](std::string::value_type c) {
            return !std::isxdigit(static_cast<unsigned char>(c));
        });

    return (ret == txid.end());
}

static bool
isValidated(LedgerMaster& ledgerMaster, std::uint32_t seq, uint256 const& hash)
{
    if (!ledgerMaster.haveLedger(seq))
        return false;

    if (seq > ledgerMaster.getValidatedLedger()->info().seq)
        return false;

    return ledgerMaster.getHashBySeq(seq) == hash;
}

bool
getMetaHex(Ledger const& ledger, uint256 const& transID, std::string& hex)
{
    SHAMapTreeNode::TNType type;
    auto const item = ledger.txMap().peekItem(transID, type);

    if (!item)
        return false;

    if (type != SHAMapTreeNode::tnTRANSACTION_MD)
        return false;

    SerialIter it(item->slice());
    it.getVL();  // skip transaction
    hex = strHex(makeSlice(it.getVL()));
    return true;
}

struct TxResult
{
    Transaction::pointer txn;
    std::variant<std::shared_ptr<TxMeta>, Blob> meta;
    bool validated = false;
    TxSearchedAll searchedAll;
};

struct TxArgs
{
    uint256 hash;
    bool binary = false;
    std::optional<std::pair<uint32_t, uint32_t>> ledgerRange;
};

std::pair<TxResult, RPC::Status>
doTxHelp(RPC::Context& context, TxArgs const& args)
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

    std::shared_ptr<Transaction> txn;
    std::shared_ptr<TxMeta> meta;
    auto ec{rpcSUCCESS};

    using TxPair =
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;

    result.searchedAll = TxSearchedAll::unknown;

    std::variant<TxPair, TxSearchedAll> v;
    if (args.ledgerRange)
    {
        v = context.app.getMasterTransaction().fetch(args.hash, range, ec);
    }
    else
    {
        v = context.app.getMasterTransaction().fetch(args.hash, ec);
    }

    if (auto e = std::get_if<TxSearchedAll>(&v))
    {
        result.searchedAll = *e;
        return {result, rpcTXN_NOT_FOUND};
    }
    else
    {
        std::tie(txn, meta) = std::get<TxPair>(v);
    }

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
        result.meta = meta;
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
            result.searchedAll != TxSearchedAll::unknown)
        {
            status = {
                grpc::StatusCode::NOT_FOUND,
                "txn not found. searched_all = " +
                    to_string(
                        (result.searchedAll == TxSearchedAll::yes ? "true"
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
            result.searchedAll != TxSearchedAll::unknown)
        {
            response = Json::Value(Json::objectValue);
            response[jss::searched_all] =
                (result.searchedAll == TxSearchedAll::yes);
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
    // Deserialize and validate JSON arguments

    if (!context.params.isMember(jss::transaction))
        return rpcError(rpcINVALID_PARAMS);

    std::string txHash = context.params[jss::transaction].asString();
    if (!isHexTxID(txHash))
        return rpcError(rpcNOT_IMPL);

    TxArgs args;
    args.hash = from_hex_text<uint256>(txHash);

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
    // return values
    org::xrpl::rpc::v1::GetTransactionResponse response;
    grpc::Status status = grpc::Status::OK;

    // input
    org::xrpl::rpc::v1::GetTransactionRequest& request = context.params;

    TxArgs args;

    std::string const& hashBytes = request.hash();
    args.hash = uint256::fromVoid(hashBytes.data());
    if (args.hash.size() != hashBytes.size())
    {
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "ledger hash malformed"};
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
