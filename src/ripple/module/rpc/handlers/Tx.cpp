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
    context.lock_.unlock ();

    if (!context.params_.isMember (jss::transaction))
        return rpcError (rpcINVALID_PARAMS);

    bool binary = context.params_.isMember (jss::binary) && context.params_[jss::binary].asBool ();

    std::string strTransaction  = context.params_[jss::transaction].asString ();

    if (Transaction::isHexTxID (strTransaction))
    {
        // transaction by ID
        uint256 txid (strTransaction);

        Transaction::pointer txn = getApp().getMasterTransaction ().fetch (txid, true);

        if (!txn)
            return rpcError (rpcTXN_NOT_FOUND);

#ifdef READY_FOR_NEW_TX_FORMAT
        Json::Value ret;
        ret[jss::transaction] = txn->getJson (0, binary);
#else
        Json::Value ret = txn->getJson (0, binary);
#endif

        if (txn->getLedger () != 0)
        {
            Ledger::pointer lgr = context.netOps_.getLedgerBySeq (txn->getLedger ());

            if (lgr)
            {
                bool okay = false;

                if (binary)
                {
                    std::string meta;

                    if (lgr->getMetaHex (txid, meta))
                    {
                        ret[jss::meta] = meta;
                        okay = true;
                    }
                }
                else
                {
                    TransactionMetaSet::pointer set;

                    if (lgr->getTransactionMeta (txid, set))
                    {
                        okay = true;
                        ret[jss::meta] = set->getJson (0);
                    }
                }

                if (okay)
                    ret[jss::validated] = context.netOps_.isValidated (lgr);
            }
        }

        return ret;
    }

    return rpcError (rpcNOT_IMPL);
}

} // ripple
