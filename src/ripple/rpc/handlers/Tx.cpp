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

namespace ripple {

// {
//   transaction: <hex>
// }
Json::Value doTx (RPC::Context& context)
{
    if (!context.params_.isMember (jss::transaction))
        return rpcError (rpcINVALID_PARAMS);

    bool binary = context.params_.isMember (jss::binary)
            && context.params_[jss::binary].asBool ();

    auto const txid  = context.params_[jss::transaction].asString ();

    if (!Transaction::isHexTxID (txid))
        return rpcError (rpcNOT_IMPL);

    auto txn = getApp().getMasterTransaction ().fetch (uint256 (txid), true);

    if (!txn)
        return rpcError (rpcTXN_NOT_FOUND);

    Json::Value ret = txn->getJson (1, binary);

    if (txn->getLedger () == 0)
        return ret;

    if (auto lgr = context.netOps_.getLedgerBySeq (txn->getLedger ()))
    {
        bool okay = false;
        if (binary)
        {
            std::string meta;

            if (lgr->getMetaHex (txn->getID (), meta))
            {
                ret[jss::meta] = meta;
                okay = true;
            }
        }
        else
        {
            TransactionMetaSet::pointer txMeta;

            if (lgr->getTransactionMeta (txn->getID (), txMeta))
            {
                okay = true;
                auto meta = txMeta->getJson (0);
                addPaymentDeliveredAmount (meta, context, txn, txMeta);
                ret[jss::meta] = meta;
            }
        }

        if (okay)
            ret[jss::validated] = context.netOps_.isValidated (lgr);
    }

    return ret;
}

} // ripple
