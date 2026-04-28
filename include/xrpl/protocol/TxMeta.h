#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>

#include <optional>

namespace xrpl {

class TxMeta
{
public:
    TxMeta(uint256 const& transactionID, std::uint32_t ledger);
    TxMeta(uint256 const& txID, std::uint32_t ledger, Blob const&);
    TxMeta(uint256 const& txID, std::uint32_t ledger, STObject const&);

    [[nodiscard]] uint256 const&
    getTxID() const
    {
        return transactionID_;
    }
    [[nodiscard]] std::uint32_t
    getLgrSeq() const
    {
        return ledgerSeq_;
    }
    [[nodiscard]] int
    getResult() const
    {
        return result_;
    }
    [[nodiscard]] TER
    getResultTER() const
    {
        return TER::fromInt(result_);
    }
    [[nodiscard]] std::uint32_t
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
    [[nodiscard]] boost::container::flat_set<AccountID>
    getAffectedAccounts() const;

    [[nodiscard]] Json::Value
    getJson(JsonOptions p) const
    {
        return getAsObject().getJson(p);
    }
    void
    addRaw(Serializer&, TER, std::uint32_t index);

    [[nodiscard]] STObject
    getAsObject() const;
    STArray&
    getNodes()
    {
        return nodes_;
    }
    [[nodiscard]] STArray const&
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

    [[nodiscard]] std::optional<STAmount> const&
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

}  // namespace xrpl
