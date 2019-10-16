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
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/GRPCHandlers.h>

namespace ripple {

// {
//   transaction: <hex>
// }

static
bool
isHexTxID (std::string const& txid)
{
    if (txid.size () != 64)
        return false;

    auto const ret = std::find_if (txid.begin (), txid.end (),
        [](std::string::value_type c)
        {
            return !std::isxdigit (static_cast<unsigned char>(c));
        });

    return (ret == txid.end ());
}

static
bool
isValidated(LedgerMaster& ledgerMaster, std::uint32_t seq, uint256 const& hash)
{
    if (!ledgerMaster.haveLedger (seq))
        return false;

    if (seq > ledgerMaster.getValidatedLedger ()->info().seq)
        return false;

    return ledgerMaster.getHashBySeq (seq) == hash;
}

static
bool
isValidated (RPC::JsonContext& context, std::uint32_t seq, uint256 const& hash)
{
    return isValidated(context.ledgerMaster, seq, hash);
}

bool
getMetaHex (Ledger const& ledger,
    uint256 const& transID, std::string& hex)
{
    SHAMapTreeNode::TNType type;
    auto const item =
        ledger.txMap().peekItem (transID, type);

    if (!item)
        return false;

    if (type != SHAMapTreeNode::tnTRANSACTION_MD)
        return false;

    SerialIter it (item->slice());
    it.getVL (); // skip transaction
    hex = strHex (makeSlice(it.getVL ()));
    return true;
}

Json::Value doTx (RPC::JsonContext& context)
{
    if (!context.params.isMember (jss::transaction))
        return rpcError (rpcINVALID_PARAMS);

    bool binary = context.params.isMember (jss::binary)
            && context.params[jss::binary].asBool ();

    auto const txid  = context.params[jss::transaction].asString ();

    if (!isHexTxID (txid))
        return rpcError (rpcNOT_IMPL);

    ClosedInterval<uint32_t> range;

    auto rangeProvided = context.params.isMember (jss::min_ledger) &&
        context.params.isMember (jss::max_ledger);

    if (rangeProvided)
    {
        try
        {
            auto const& min = context.params[jss::min_ledger].asUInt ();
            auto const& max = context.params[jss::max_ledger].asUInt ();

            constexpr uint16_t MAX_RANGE = 1000;

            if (max < min)
                return rpcError (rpcINVALID_LGR_RANGE);

            if (max - min > MAX_RANGE)
                return rpcError (rpcEXCESSIVE_LGR_RANGE);

            range = ClosedInterval<uint32_t> (min, max);
        }
        catch (...)
        {
            // One of the calls to `asUInt ()` failed.
            return rpcError (rpcINVALID_LGR_RANGE);
        }
    }

    using pointer = Transaction::pointer;

    auto ec {rpcSUCCESS};
    pointer txn;

    if (rangeProvided)
    {
        boost::variant<pointer, bool> v =
            context.app.getMasterTransaction().fetch(
                from_hex_text<uint256>(txid), range, ec);

        if (v.which () == 1)
        {
            auto jvResult = Json::Value (Json::objectValue);

            jvResult[jss::searched_all] = boost::get<bool> (v);

            return rpcError (rpcTXN_NOT_FOUND, jvResult);
        }
        else
            txn = boost::get<pointer> (v);
    }
    else
        txn = context.app.getMasterTransaction().fetch(
            from_hex_text<uint256>(txid), ec);

    if (ec == rpcDB_DESERIALIZATION)
        return rpcError (ec);

    if (!txn)
        return rpcError (rpcTXN_NOT_FOUND);

    Json::Value ret = txn->getJson (JsonOptions::include_date, binary);

    if (txn->getLedger () == 0)
        return ret;

    if (auto lgr = context.ledgerMaster.getLedgerBySeq (txn->getLedger ()))
    {
        bool okay = false;

        if (binary)
        {
            std::string meta;

            if (getMetaHex (*lgr, txn->getID (), meta))
            {
                ret[jss::meta] = meta;
                okay = true;
            }
        }
        else
        {
            auto rawMeta = lgr->txRead (txn->getID()).second;
            if (rawMeta)
            {
                auto txMeta = std::make_shared<TxMeta>(
                    txn->getID(), lgr->seq(), *rawMeta);
                okay = true;
                auto meta = txMeta->getJson (JsonOptions::none);
                insertDeliveredAmount (meta, context, txn, *txMeta);
                ret[jss::meta] = std::move(meta);
            }
        }

        if (okay)
            ret[jss::validated] = isValidated (
                context, lgr->info().seq, lgr->info().hash);
    }

    return ret;
}

std::pair<rpc::v1::GetTxResponse, grpc::Status>
doTxGrpc(RPC::GRPCContext<rpc::v1::GetTxRequest>& context)
{
    // return values
    rpc::v1::GetTxResponse result;
    grpc::Status status = grpc::Status::OK;

    // input
    rpc::v1::GetTxRequest& request = context.params;

    std::string const& hashBytes = request.hash();
    uint256 hash = uint256::fromVoid(hashBytes.data());

    // hash is included in the response
    result.set_hash(request.hash());

    auto ec{rpcSUCCESS};

    // get the transaction
    std::shared_ptr<Transaction> txn =
        context.app.getMasterTransaction().fetch(hash, ec);

    if (ec == rpcDB_DESERIALIZATION)
    {
        auto errorInfo = RPC::get_error_info(ec);
        grpc::Status errorStatus{grpc::StatusCode::INTERNAL,
                                 errorInfo.message.c_str()};
        return {result, errorStatus};
    }
    if (!txn)
    {
        grpc::Status errorStatus{grpc::StatusCode::NOT_FOUND, "txn not found"};
        return {result, errorStatus};
    }

    std::shared_ptr<STTx const> stTxn = txn->getSTransaction();
    if (stTxn->getTxnType() != ttPAYMENT)
    {
        auto getTypeStr = [&stTxn]() {
            return TxFormats::getInstance()
                .findByType(stTxn->getTxnType())
                ->getName();
        };

        grpc::Status errorStatus{grpc::StatusCode::UNIMPLEMENTED,
                                 "txn type not supported: " + getTypeStr()};
        return {result, errorStatus};
    }

    // populate transaction data
    if (request.binary())
    {
        Serializer s = stTxn->getSerializer();
        result.set_transaction_binary(s.data(), s.size());
    }
    else
    {
        RPC::populateTransaction(*result.mutable_transaction(), stTxn);
    }

    result.set_ledger_index(txn->getLedger());

    std::shared_ptr<Ledger const> ledger =
        context.ledgerMaster.getLedgerBySeq(txn->getLedger());
    // get meta data
    if (ledger)
    {
        if (request.binary())
        {
            SHAMapTreeNode::TNType type;
            auto const item = ledger->txMap().peekItem(txn->getID(), type);

            if (item && type == SHAMapTreeNode::tnTRANSACTION_MD)
            {
                SerialIter it(item->slice());
                it.skip(it.getVLDataLength());  // skip transaction
                Blob blob = it.getVL();
                Slice slice = makeSlice(blob);
                result.set_meta_binary(slice.data(), slice.size());

                bool validated = isValidated(
                    context.ledgerMaster,
                    ledger->info().seq,
                    ledger->info().hash);
                result.set_validated(validated);
            }
        }
        else
        {
            auto rawMeta = ledger->txRead(txn->getID()).second;
            if (rawMeta)
            {
                auto txMeta = std::make_shared<TxMeta>(
                    txn->getID(), ledger->seq(), *rawMeta);

                bool validated = isValidated(
                    context.ledgerMaster,
                    ledger->info().seq,
                    ledger->info().hash);
                result.set_validated(validated);

                RPC::populateMeta(*result.mutable_meta(), txMeta);
                insertDeliveredAmount(
                    *result.mutable_meta()->mutable_delivered_amount(),
                    context,
                    txn,
                    *txMeta);
            }
        }
    }
    return {result, status};
}

}  // namespace ripple
