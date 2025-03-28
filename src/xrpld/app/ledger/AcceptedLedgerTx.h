//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_LEDGER_ACCEPTEDLEDGERTX_H_INCLUDED
#define RIPPLE_APP_LEDGER_ACCEPTEDLEDGERTX_H_INCLUDED

#include <xrpld/app/ledger/Ledger.h>

#include <xrpl/protocol/AccountID.h>

#include <boost/container/flat_set.hpp>

namespace ripple {

class Logs;

/**
    A transaction that is in a closed ledger.

    Description

    An accepted ledger transaction contains additional information that the
    server needs to tell clients about the transaction. For example,
        - The transaction in JSON form
        - Which accounts are affected
          * This is used by InfoSub to report to clients
        - Cached stuff
*/
class AcceptedLedgerTx : public CountedObject<AcceptedLedgerTx>
{
public:
    AcceptedLedgerTx(
        std::shared_ptr<ReadView const> const& ledger,
        std::shared_ptr<STTx const> const&,
        std::shared_ptr<STObject const> const&);

    std::shared_ptr<STTx const> const&
    getTxn() const
    {
        return mTxn;
    }
    TxMeta const&
    getMeta() const
    {
        return mMeta;
    }

    boost::container::flat_set<AccountID> const&
    getAffected() const
    {
        return mAffected;
    }

    TxID
    getTransactionID() const
    {
        return mTxn->getTransactionID();
    }
    TxType
    getTxnType() const
    {
        return mTxn->getTxnType();
    }
    TER
    getResult() const
    {
        return mMeta.getResultTER();
    }
    std::uint32_t
    getTxnSeq() const
    {
        return mMeta.getIndex();
    }
    std::string
    getEscMeta() const;

    Json::Value const&
    getJson() const
    {
        return mJson;
    }

private:
    std::shared_ptr<STTx const> mTxn;
    TxMeta mMeta;
    boost::container::flat_set<AccountID> mAffected;
    Blob mRawMeta;
    Json::Value mJson;
};

}  // namespace ripple

#endif
