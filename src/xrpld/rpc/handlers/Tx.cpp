#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/rpc/CTID.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/Status.h>

#include <xrpl/basics/ToString.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/server/NetworkOPs.h>

#include <regex>

namespace xrpl {

static bool
isValidated(LedgerMaster& ledgerMaster, std::uint32_t seq, uint256 const& hash)
{
    if (!ledgerMaster.haveLedger(seq))
        return false;

    if (seq > ledgerMaster.getValidatedLedger()->header().seq)
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

        range = ClosedInterval<uint32_t>(args.ledgerRange->first, args.ledgerRange->second);
    }

    auto ec{rpcSUCCESS};

    using TxPair = std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;

    result.searchedAll = TxSearched::unknown;
    std::variant<TxPair, TxSearched> v;

    if (args.ctid)
    {
        args.hash =
            context.app.getLedgerMaster().txnIdFromIndex(args.ctid->first, args.ctid->second);

        if (args.hash)
            range = ClosedInterval<uint32_t>(args.ctid->first, args.ctid->second);
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

    std::shared_ptr<Ledger const> ledger = context.ledgerMaster.getLedgerBySeq(txn->getLedger());

    if (ledger && !ledger->open())
        result.ledgerHash = ledger->header().hash;

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
        result.validated =
            isValidated(context.ledgerMaster, ledger->header().seq, ledger->header().hash);
        if (result.validated)
            result.closeTime = context.ledgerMaster.getCloseTimeBySeq(txn->getLedger());

        // compute outgoing CTID
        if (meta->getAsObject().isFieldPresent(sfTransactionIndex))
        {
            uint32_t lgrSeq = ledger->header().seq;
            uint32_t txnIdx = meta->getAsObject().getFieldU32(sfTransactionIndex);
            uint32_t netID = context.app.getNetworkIDService().getNetworkID();

            if (txnIdx <= 0xFFFFU && netID < 0xFFFFU && lgrSeq < 0x0FFF'FFFFUL)
                result.ctid = RPC::encodeCTID(lgrSeq, (uint32_t)txnIdx, (uint32_t)netID);
        }
    }

    return {result, rpcSUCCESS};
}

// Helper function to fetch and format inner transaction results for Batch
// transactions
static Expected<void, Json::Value>
insertBatchInnerTransactions(
    Json::Value& response,
    std::shared_ptr<STTx const> const& sttx,
    TxArgs const& args,
    RPC::JsonContext const& context)
{
    auto const& innerTxnIds = sttx->getBatchTransactionIDs();
    if (innerTxnIds.empty())
    {
        JLOG(context.j.error()) << "Batch transaction " << sttx->getTransactionID()
                                << " has no inner transactions";
        return Unexpected(RPC::make_error(rpcINTERNAL));
    }

    Json::Value innerTxns(Json::arrayValue);

    for (auto const& innerTxnId : innerTxnIds)
    {
        Json::Value innerResult(Json::objectValue);
        innerResult[jss::hash] = to_string(innerTxnId);

        // Fetch the inner transaction
        error_code_i errorCode = rpcSUCCESS;
        auto txData = context.app.getMasterTransaction().fetch(innerTxnId, errorCode);
        if (errorCode != rpcSUCCESS)
        {
            return Unexpected(RPC::make_error(errorCode));
        }

        if (auto txPair =
                std::get_if<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>>(
                    &txData))
        {
            auto const& [innerTxn, innerMeta] = *txPair;
            if (innerTxn && innerMeta)
            {
                // Add transaction result code
                auto const ter = innerMeta->getResultTER();
                innerResult[jss::engine_result] = transToken(ter);

                if (!args.binary)
                {
                    // Add the full transaction JSON
                    auto const& innerSttx = innerTxn->getSTransaction();
                    if (context.apiVersion > 1)
                    {
                        constexpr auto options =
                            JsonOptions::include_date | JsonOptions::disable_API_prior_V2;
                        innerResult[jss::tx_json] = innerTxn->getJson(options);
                    }
                    else
                    {
                        innerResult[jss::tx_json] = innerTxn->getJson(JsonOptions::include_date);
                    }

                    // Add metadata
                    innerResult[jss::meta] = innerMeta->getJson(JsonOptions::none);
                    RPC::insertDeliverMax(
                        innerResult[jss::tx_json], innerSttx->getTxnType(), context.apiVersion);
                    insertDeliveredAmount(innerResult[jss::meta], context, innerTxn, *innerMeta);
                    RPC::insertNFTSyntheticInJson(innerResult, innerSttx, *innerMeta);
                    RPC::insertMPTokenIssuanceID(innerResult[jss::meta], innerSttx, *innerMeta);
                }
                else
                {
                    // Binary mode
                    if (context.apiVersion > 1)
                    {
                        innerResult[jss::tx_blob] = innerTxn->getJson(
                            JsonOptions::include_date | JsonOptions::disable_API_prior_V2, true);
                    }
                    else
                    {
                        innerResult[jss::tx_blob] =
                            innerTxn->getJson(JsonOptions::include_date, true);
                    }
                    innerResult[jss::meta_blob] =
                        strHex(innerMeta->getAsObject().getSerializer().getData());
                }
            }
        }

        innerTxns.append(innerResult);
    }

    response[jss::inner_transactions] = innerTxns;
    return {};
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
        if (error.toErrorCode() == rpcTXN_NOT_FOUND && result.searchedAll != TxSearched::unknown)
        {
            response = Json::Value(Json::objectValue);
            response[jss::searched_all] = (result.searchedAll == TxSearched::all);
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
                    response[jss::tx_json], sttx->getTxnType(), context.apiVersion);
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
                    response[jss::close_time_iso] = to_string_iso(*result.closeTime);
            }
        }
        else
        {
            response = result.txn->getJson(JsonOptions::include_date, args.binary);
            if (!args.binary)
                RPC::insertDeliverMax(response, sttx->getTxnType(), context.apiVersion);
        }

        // populate binary metadata
        if (auto blob = std::get_if<Blob>(&result.meta))
        {
            XRPL_ASSERT(args.binary, "xrpl::populateJsonResponse : binary is set");
            auto json_meta = (context.apiVersion > 1 ? jss::meta_blob : jss::meta);
            response[json_meta] = strHex(makeSlice(*blob));
        }
        // populate meta data
        else if (auto m = std::get_if<std::shared_ptr<TxMeta>>(&result.meta))
        {
            auto& meta = *m;
            if (meta)
            {
                response[jss::meta] = meta->getJson(JsonOptions::none);
                insertDeliveredAmount(response[jss::meta], context, result.txn, *meta);
                RPC::insertNFTSyntheticInJson(response, sttx, *meta);
                RPC::insertMPTokenIssuanceID(response[jss::meta], sttx, *meta);
            }
        }
        response[jss::validated] = result.validated;

        if (result.ctid)
            response[jss::ctid] = *(result.ctid);

        // For Batch transactions, include inner transaction results
        if (result.validated && sttx->getTxnType() == ttBATCH)
        {
            auto result = insertBatchInnerTransactions(response, sttx, args, context);
            if (!result)
            {
                return result.error();
            }
        }
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

    if (context.params.isMember(jss::transaction) && context.params.isMember(jss::ctid))
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
        if (net_id != context.app.getNetworkIDService().getNetworkID())
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

    args.binary = context.params.isMember(jss::binary) && context.params[jss::binary].asBool();

    if (context.params.isMember(jss::min_ledger) && context.params.isMember(jss::max_ledger))
    {
        try
        {
            args.ledgerRange = std::make_pair(
                context.params[jss::min_ledger].asUInt(), context.params[jss::max_ledger].asUInt());
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

}  // namespace xrpl
