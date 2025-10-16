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

template <class T>
TxMeta::TxMeta(
    uint256 const& txid,
    std::uint32_t ledger,
    T const& data,
    CtorHelper)
    : mTransactionID(txid), mLedger(ledger), mNodes(sfAffectedNodes, 32)
{
    SerialIter sit(makeSlice(data));

    STObject obj(sit, sfMetadata);
    mResult = obj.getFieldU8(sfTransactionResult);
    mIndex = obj.getFieldU32(sfTransactionIndex);
    mNodes = *dynamic_cast<STArray*>(&obj.getField(sfAffectedNodes));

    if (obj.isFieldPresent(sfDeliveredAmount))
        setDeliveredAmount(obj.getFieldAmount(sfDeliveredAmount));

    if (obj.isFieldPresent(sfParentBatchID))
        setParentBatchId(obj.getFieldH256(sfParentBatchID));
}

TxMeta::TxMeta(uint256 const& txid, std::uint32_t ledger, STObject const& obj)
    : mTransactionID(txid)
    , mLedger(ledger)
    , mNodes(obj.getFieldArray(sfAffectedNodes))
{
    mResult = obj.getFieldU8(sfTransactionResult);
    mIndex = obj.getFieldU32(sfTransactionIndex);

    auto affectedNodes =
        dynamic_cast<STArray const*>(obj.peekAtPField(sfAffectedNodes));
    XRPL_ASSERT(
        affectedNodes,
        "ripple::TxMeta::TxMeta(STObject) : type cast succeeded");
    if (affectedNodes)
        mNodes = *affectedNodes;

    if (obj.isFieldPresent(sfDeliveredAmount))
        setDeliveredAmount(obj.getFieldAmount(sfDeliveredAmount));

    if (obj.isFieldPresent(sfParentBatchID))
        setParentBatchId(obj.getFieldH256(sfParentBatchID));
}

TxMeta::TxMeta(uint256 const& txid, std::uint32_t ledger, Blob const& vec)
    : TxMeta(txid, ledger, vec, CtorHelper())
{
}

TxMeta::TxMeta(
    uint256 const& txid,
    std::uint32_t ledger,
    std::string const& data)
    : TxMeta(txid, ledger, data, CtorHelper())
{
}

TxMeta::TxMeta(
    uint256 const& transactionID,
    std::uint32_t ledger,
    std::optional<uint256> parentBatchId)
    : mTransactionID(transactionID)
    , mLedger(ledger)
    , mIndex(static_cast<std::uint32_t>(-1))
    , mResult(255)
    , mParentBatchId(parentBatchId)
    , mNodes(sfAffectedNodes)
{
    mNodes.reserve(32);
}

void
TxMeta::setAffectedNode(
    uint256 const& node,
    SField const& type,
    std::uint16_t nodeType)
{
    // make sure the node exists and force its type
    for (auto& n : mNodes)
    {
        if (n.getFieldH256(sfLedgerIndex) == node)
        {
            n.setFName(type);
            n.setFieldU16(sfLedgerEntryType, nodeType);
            return;
        }
    }

    mNodes.push_back(STObject(type));
    STObject& obj = mNodes.back();

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
    for (auto const& it : mNodes)
    {
        int index = it.getFieldIndex(
            (it.getFName() == sfCreatedNode) ? sfNewFields : sfFinalFields);

        if (index != -1)
        {
            auto inner = dynamic_cast<STObject const*>(&it.peekAtIndex(index));
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
    for (auto& n : mNodes)
    {
        if (n.getFieldH256(sfLedgerIndex) == index)
            return n;
    }
    mNodes.push_back(STObject(type));
    STObject& obj = mNodes.back();

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
    for (auto& n : mNodes)
    {
        if (n.getFieldH256(sfLedgerIndex) == node)
            return n;
    }
    // LCOV_EXCL_START
    UNREACHABLE("ripple::TxMeta::getAffectedNode(uint256) : node not found");
    Throw<std::runtime_error>("Affected node not found");
    return *(mNodes.begin());  // Silence compiler warning.
    // LCOV_EXCL_STOP
}

STObject
TxMeta::getAsObject() const
{
    STObject metaData(sfTransactionMetaData);
    XRPL_ASSERT(mResult != 255, "ripple::TxMeta::getAsObject : result is set");
    metaData.setFieldU8(sfTransactionResult, mResult);
    metaData.setFieldU32(sfTransactionIndex, mIndex);
    metaData.emplace_back(mNodes);
    if (hasDeliveredAmount())
        metaData.setFieldAmount(sfDeliveredAmount, getDeliveredAmount());

    if (hasParentBatchId())
        metaData.setFieldH256(sfParentBatchID, getParentBatchId());

    return metaData;
}

void
TxMeta::addRaw(Serializer& s, TER result, std::uint32_t index)
{
    mResult = TERtoInt(result);
    mIndex = index;
    XRPL_ASSERT(
        (mResult == 0) || ((mResult > 100) && (mResult <= 255)),
        "ripple::TxMeta::addRaw : valid TER input");

    mNodes.sort([](STObject const& o1, STObject const& o2) {
        return o1.getFieldH256(sfLedgerIndex) < o2.getFieldH256(sfLedgerIndex);
    });

    getAsObject().add(s);
}

}  // namespace ripple
