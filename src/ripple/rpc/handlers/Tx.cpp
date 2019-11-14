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
isValidated (RPC::Context& context, std::uint32_t seq, uint256 const& hash)
{
    if (!context.ledgerMaster.haveLedger (seq))
        return false;

    if (seq > context.ledgerMaster.getValidatedLedger ()->info().seq)
        return false;

    return context.ledgerMaster.getHashBySeq (seq) == hash;
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

Json::Value doTx (RPC::Context& context)
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

    auto searchedAll = false;
    auto ec {rpcSUCCESS};
    pointer txn;

    if (rangeProvided)
    {
        boost::variant<Transaction::pointer, bool> v =
            context.app.getMasterTransaction().fetch(
                from_hex_text<uint256>(txid), range, ec);

        if (v.which () != 0 || !boost::get<pointer> (v))
            searchedAll = boost::get<bool> (v);
        else
            txn = boost::get<pointer> (v);
    }
    else
        txn = context.app.getMasterTransaction().fetch(
            from_hex_text<uint256>(txid), ec);

    if (ec == rpcDB_DESERIALIZATION)
        return rpcError (ec);

    if (!txn)
    {
        auto jvResult = Json::Value (Json::objectValue);

        if (rangeProvided)
            jvResult[jss::searched_all] = searchedAll;

        return rpcError (rpcTXN_NOT_FOUND, jvResult);
    }

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

} // ripple
