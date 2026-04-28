#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <boost/container/flat_set.hpp>

namespace xrpl {

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

    [[nodiscard]] std::shared_ptr<STTx const> const&
    getTxn() const
    {
        return mTxn;
    }
    [[nodiscard]] TxMeta const&
    getMeta() const
    {
        return mMeta;
    }

    [[nodiscard]] boost::container::flat_set<AccountID> const&
    getAffected() const
    {
        return mAffected;
    }

    [[nodiscard]] TxID
    getTransactionID() const
    {
        return mTxn->getTransactionID();
    }
    [[nodiscard]] TxType
    getTxnType() const
    {
        return mTxn->getTxnType();
    }
    [[nodiscard]] TER
    getResult() const
    {
        return mMeta.getResultTER();
    }
    [[nodiscard]] std::uint32_t
    getTxnSeq() const
    {
        return mMeta.getIndex();
    }
    [[nodiscard]] std::string
    getEscMeta() const;

    [[nodiscard]] Json::Value const&
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

}  // namespace xrpl
