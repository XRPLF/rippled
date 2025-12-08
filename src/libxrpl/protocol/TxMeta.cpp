#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>

#include <boost/container/flat_set.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace ripple {

TxMeta::TxMeta(uint256 const& txid, std::uint32_t ledger, STObject const& obj)
    : transactionID_(txid)
    , ledgerSeq_(ledger)
    , nodes_(obj.getFieldArray(sfAffectedNodes))
{
    result_ = obj.getFieldU8(sfTransactionResult);
    index_ = obj.getFieldU32(sfTransactionIndex);

    auto affectedNodes =
        dynamic_cast<STArray const*>(obj.peekAtPField(sfAffectedNodes));
    XRPL_ASSERT(
        affectedNodes,
        "ripple::TxMeta::TxMeta(STObject) : type cast succeeded");
    if (affectedNodes)
        nodes_ = *affectedNodes;

    setAdditionalFields(obj);
}

TxMeta::TxMeta(uint256 const& txid, std::uint32_t ledger, Blob const& vec)
    : transactionID_(txid), ledgerSeq_(ledger), nodes_(sfAffectedNodes, 32)
{
    SerialIter sit(makeSlice(vec));

    STObject obj(sit, sfMetadata);
    result_ = obj.getFieldU8(sfTransactionResult);
    index_ = obj.getFieldU32(sfTransactionIndex);
    nodes_ = obj.getFieldArray(sfAffectedNodes);

    setAdditionalFields(obj);
}

TxMeta::TxMeta(uint256 const& transactionID, std::uint32_t ledger)
    : transactionID_(transactionID)
    , ledgerSeq_(ledger)
    , index_(std::numeric_limits<std::uint32_t>::max())
    , result_(255)
    , nodes_(sfAffectedNodes)
{
    nodes_.reserve(32);
}

void
TxMeta::setAffectedNode(
    uint256 const& node,
    SField const& type,
    std::uint16_t nodeType)
{
    // make sure the node exists and force its type
    for (auto& n : nodes_)
    {
        if (n.getFieldH256(sfLedgerIndex) == node)
        {
            n.setFName(type);
            n.setFieldU16(sfLedgerEntryType, nodeType);
            return;
        }
    }

    nodes_.push_back(STObject(type));
    STObject& obj = nodes_.back();

    XRPL_ASSERT(
        obj.getFName() == type,
        "ripple::TxMeta::setAffectedNode : field type match");
    obj.setFieldH256(sfLedgerIndex, node);
    obj.setFieldU16(sfLedgerEntryType, nodeType);
}

boost::container::flat_set<AccountID>
TxMeta::getAffectedAccounts() const
{
    boost::container::flat_set<AccountID> list;
    list.reserve(10);

    // This code should match the behavior of the JS method:
    // Meta#getAffectedAccounts
    for (auto const& node : nodes_)
    {
        int index = node.getFieldIndex(
            (node.getFName() == sfCreatedNode) ? sfNewFields : sfFinalFields);

        if (index != -1)
        {
            auto const* inner =
                dynamic_cast<STObject const*>(&node.peekAtIndex(index));
            XRPL_ASSERT(
                inner,
                "ripple::getAffectedAccounts : STObject type cast succeeded");
            if (inner)
            {
                for (auto const& field : *inner)
                {
                    if (auto sa = dynamic_cast<STAccount const*>(&field))
                    {
                        XRPL_ASSERT(
                            !sa->isDefault(),
                            "ripple::getAffectedAccounts : account is set");
                        if (!sa->isDefault())
                            list.insert(sa->value());
                    }
                    else if (
                        (field.getFName() == sfLowLimit) ||
                        (field.getFName() == sfHighLimit) ||
                        (field.getFName() == sfTakerPays) ||
                        (field.getFName() == sfTakerGets))
                    {
                        auto lim = dynamic_cast<STAmount const*>(&field);
                        XRPL_ASSERT(
                            lim,
                            "ripple::getAffectedAccounts : STAmount type cast "
                            "succeeded");

                        if (lim != nullptr)
                        {
                            auto issuer = lim->getIssuer();

                            if (issuer.isNonZero())
                                list.insert(issuer);
                        }
                    }
                    else if (field.getFName() == sfMPTokenIssuanceID)
                    {
                        auto mptID =
                            dynamic_cast<STBitString<192> const*>(&field);
                        if (mptID != nullptr)
                        {
                            auto issuer = MPTIssue(mptID->value()).getIssuer();

                            if (issuer.isNonZero())
                                list.insert(issuer);
                        }
                    }
                }
            }
        }
    }

    return list;
}

STObject&
TxMeta::getAffectedNode(SLE::ref node, SField const& type)
{
    uint256 index = node->key();
    for (auto& n : nodes_)
    {
        if (n.getFieldH256(sfLedgerIndex) == index)
            return n;
    }
    nodes_.push_back(STObject(type));
    STObject& obj = nodes_.back();

    XRPL_ASSERT(
        obj.getFName() == type,
        "ripple::TxMeta::getAffectedNode(SLE::ref) : field type match");
    obj.setFieldH256(sfLedgerIndex, index);
    obj.setFieldU16(sfLedgerEntryType, node->getFieldU16(sfLedgerEntryType));

    return obj;
}

STObject&
TxMeta::getAffectedNode(uint256 const& node)
{
    for (auto& n : nodes_)
    {
        if (n.getFieldH256(sfLedgerIndex) == node)
            return n;
    }
    // LCOV_EXCL_START
    UNREACHABLE("ripple::TxMeta::getAffectedNode(uint256) : node not found");
    Throw<std::runtime_error>("Affected node not found");
    return *(nodes_.begin());  // Silence compiler warning.
    // LCOV_EXCL_STOP
}

STObject
TxMeta::getAsObject() const
{
    STObject metaData(sfTransactionMetaData);
    XRPL_ASSERT(result_ != 255, "ripple::TxMeta::getAsObject : result_ is set");
    metaData.setFieldU8(sfTransactionResult, result_);
    metaData.setFieldU32(sfTransactionIndex, index_);
    metaData.emplace_back(nodes_);
    if (deliveredAmount_.has_value())
        metaData.setFieldAmount(sfDeliveredAmount, *deliveredAmount_);

    if (parentBatchID_.has_value())
        metaData.setFieldH256(sfParentBatchID, *parentBatchID_);

    return metaData;
}

void
TxMeta::addRaw(Serializer& s, TER result, std::uint32_t index)
{
    result_ = TERtoInt(result);
    index_ = index;
    XRPL_ASSERT(
        (result_ == 0) || ((result_ > 100) && (result_ <= 255)),
        "ripple::TxMeta::addRaw : valid TER input");

    nodes_.sort([](STObject const& o1, STObject const& o2) {
        return o1.getFieldH256(sfLedgerIndex) < o2.getFieldH256(sfLedgerIndex);
    });

    getAsObject().add(s);
}

}  // namespace ripple
