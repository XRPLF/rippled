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

#ifndef XRPL_APP_TX_TRANSACTIONMETA_H_INCLUDED
#define XRPL_APP_TX_TRANSACTIONMETA_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>

#include <optional>

namespace ripple {

class TxMeta
{
public:
    TxMeta(uint256 const& transactionID, std::uint32_t ledger);
    TxMeta(uint256 const& txID, std::uint32_t ledger, Blob const&);
    TxMeta(uint256 const& txID, std::uint32_t ledger, STObject const&);

    uint256 const&
    getTxID() const
    {
        return transactionID_;
    }
    std::uint32_t
    getLgrSeq() const
    {
        return ledgerSeq_;
    }
    int
    getResult() const
    {
        return result_;
    }
    TER
    getResultTER() const
    {
        return TER::fromInt(result_);
    }
    std::uint32_t
    getIndex() const
    {
        return index_;
    }

    void
    setAffectedNode(uint256 const&, SField const& type, std::uint16_t nodeType);
    STObject&
    getAffectedNode(SLE::ref node, SField const& type);  // create if needed
    STObject&
    getAffectedNode(uint256 const&);

    /** Return a list of accounts affected by this transaction */
    boost::container::flat_set<AccountID>
    getAffectedAccounts() const;

    Json::Value
    getJson(JsonOptions p) const
    {
        return getAsObject().getJson(p);
    }
    void
    addRaw(Serializer&, TER, std::uint32_t index);

    STObject
    getAsObject() const;
    STArray&
    getNodes()
    {
        return nodes_;
    }
    STArray const&
    getNodes() const
    {
        return nodes_;
    }

    void
    setAdditionalFields(STObject const& obj)
    {
        if (obj.isFieldPresent(sfDeliveredAmount))
            deliveredAmount_ = obj.getFieldAmount(sfDeliveredAmount);

        if (obj.isFieldPresent(sfParentBatchID))
            parentBatchID_ = obj.getFieldH256(sfParentBatchID);
    }

    std::optional<STAmount> const&
    getDeliveredAmount() const
    {
        return deliveredAmount_;
    }

    void
    setDeliveredAmount(std::optional<STAmount> const& amount)
    {
        deliveredAmount_ = amount;
    }

    void
    setParentBatchID(std::optional<uint256> const& id)
    {
        parentBatchID_ = id;
    }

private:
    uint256 transactionID_;
    std::uint32_t ledgerSeq_;
    std::uint32_t index_;
    int result_;

    std::optional<STAmount> deliveredAmount_;
    std::optional<uint256> parentBatchID_;

    STArray nodes_;
};

}  // namespace ripple

#endif
