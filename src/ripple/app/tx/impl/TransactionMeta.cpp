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

#include <BeastConfig.h>
#include <ripple/app/tx/TransactionMeta.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/STAccount.h>
#include <string>

namespace ripple {

// VFALCO TODO rename class to TransactionMeta

template<class T>
TransactionMetaSet::TransactionMetaSet (uint256 const& txid,
    std::uint32_t ledger, T const& data, CtorHelper)
    : mTransactionID (txid)
    , mLedger (ledger)
    , mNodes (sfAffectedNodes, 32)
{
    Serializer s (data);
    SerialIter sit (s);

    STObject obj(sit, sfMetadata);
    mResult = obj.getFieldU8 (sfTransactionResult);
    mIndex = obj.getFieldU32 (sfTransactionIndex);
    mNodes = * dynamic_cast<STArray*> (&obj.getField (sfAffectedNodes));

    if (obj.isFieldPresent (sfDeliveredAmount))
        setDeliveredAmount (obj.getFieldAmount (sfDeliveredAmount));
}

TransactionMetaSet::TransactionMetaSet (uint256 const& txid,
                                        std::uint32_t ledger,
                                        Blob const& vec)
    : TransactionMetaSet (txid, ledger, vec, CtorHelper ())
{
}

TransactionMetaSet::TransactionMetaSet (uint256 const& txid,
                                        std::uint32_t ledger,
                                        std::string const& data)
    : TransactionMetaSet (txid, ledger, data, CtorHelper ())
{
}

bool TransactionMetaSet::isNodeAffected (uint256 const& node) const
{
    for (auto const& n : mNodes)
    {
        if (n.getFieldH256 (sfLedgerIndex) == node)
            return true;
    }

    return false;
}

void TransactionMetaSet::setAffectedNode (uint256 const& node, SField const& type,
                                          std::uint16_t nodeType)
{
    // make sure the node exists and force its type
    for (auto& n : mNodes)
    {
        if (n.getFieldH256 (sfLedgerIndex) == node)
        {
            n.setFName (type);
            n.setFieldU16 (sfLedgerEntryType, nodeType);
            return;
        }
    }

    mNodes.push_back (STObject (type));
    STObject& obj = mNodes.back ();

    assert (obj.getFName () == type);
    obj.setFieldH256 (sfLedgerIndex, node);
    obj.setFieldU16 (sfLedgerEntryType, nodeType);
}

static void addIfUnique (std::vector<RippleAddress>& vector, RippleAddress const& address)
{
    for (auto const& a : vector)
    {
        if (a == address)
            return;
    }

    vector.push_back (address);
}

std::vector<RippleAddress> TransactionMetaSet::getAffectedAccounts ()
{
    std::vector<RippleAddress> accounts;
    accounts.reserve (10);

    // This code should match the behavior of the JS method:
    // Meta#getAffectedAccounts
    for (auto const& it : mNodes)
    {
        int index = it.getFieldIndex ((it.getFName () == sfCreatedNode) ? sfNewFields : sfFinalFields);

        if (index != -1)
        {
            const STObject* inner = dynamic_cast<const STObject*> (&it.peekAtIndex (index));

            if (inner)
            {
                for (auto const& field : *inner)
                {
                    const STAccount* sa = dynamic_cast<const STAccount*> (&field);

                    if (sa)
                        addIfUnique (accounts, sa->getValueNCA ());
                    else if ((field.getFName () == sfLowLimit) || (field.getFName () == sfHighLimit) ||
                             (field.getFName () == sfTakerPays) || (field.getFName () == sfTakerGets))
                    {
                        const STAmount* lim = dynamic_cast<const STAmount*> (&field);

                        if (lim != nullptr)
                        {
                            auto issuer = lim->getIssuer ();

                            if (issuer.isNonZero ())
                            {
                                RippleAddress na;
                                na.setAccountID (issuer);
                                addIfUnique (accounts, na);
                            }
                        }
                        else
                        {
                            WriteLog (lsFATAL, TransactionMetaSet) << "limit is not amount " << field.getJson (0);
                        }
                    }
                }
            }
            else assert (false);
        }
    }

    return accounts;
}

STObject& TransactionMetaSet::getAffectedNode (SLE::ref node, SField const& type)
{
    assert (&type);
    uint256 index = node->getIndex ();
    for (auto& n : mNodes)
    {
        if (n.getFieldH256 (sfLedgerIndex) == index)
            return n;
    }
    mNodes.push_back (STObject (type));
    STObject& obj = mNodes.back ();

    assert (obj.getFName () == type);
    obj.setFieldH256 (sfLedgerIndex, index);
    obj.setFieldU16 (sfLedgerEntryType, node->getFieldU16 (sfLedgerEntryType));

    return obj;
}

STObject& TransactionMetaSet::getAffectedNode (uint256 const& node)
{
    for (auto& n : mNodes)
    {
        if (n.getFieldH256 (sfLedgerIndex) == node)
            return n;
    }
    assert (false);
    throw std::runtime_error ("Affected node not found");
}

const STObject& TransactionMetaSet::peekAffectedNode (uint256 const& node) const
{
    for (auto const& n : mNodes)
    {
        if (n.getFieldH256 (sfLedgerIndex) == node)
            return n;
    }

    throw std::runtime_error ("Affected node not found");
}

void TransactionMetaSet::init (uint256 const& id, std::uint32_t ledger)
{
    mTransactionID = id;
    mLedger = ledger;
    mNodes = STArray (sfAffectedNodes, 32);
    mDelivered = boost::optional <STAmount> ();
}

void TransactionMetaSet::swap (TransactionMetaSet& s) noexcept
{
    assert ((mTransactionID == s.mTransactionID) && (mLedger == s.mLedger));
    mNodes.swap (s.mNodes);
}

bool TransactionMetaSet::thread (STObject& node, uint256 const& prevTxID, std::uint32_t prevLgrID)
{
    if (node.getFieldIndex (sfPreviousTxnID) == -1)
    {
        assert (node.getFieldIndex (sfPreviousTxnLgrSeq) == -1);
        node.setFieldH256 (sfPreviousTxnID, prevTxID);
        node.setFieldU32 (sfPreviousTxnLgrSeq, prevLgrID);
        return true;
    }

    assert (node.getFieldH256 (sfPreviousTxnID) == prevTxID);
    assert (node.getFieldU32 (sfPreviousTxnLgrSeq) == prevLgrID);
    return false;
}

static bool compare (const STObject& o1, const STObject& o2)
{
    return o1.getFieldH256 (sfLedgerIndex) < o2.getFieldH256 (sfLedgerIndex);
}

STObject TransactionMetaSet::getAsObject () const
{
    STObject metaData (sfTransactionMetaData);
    assert (mResult != 255);
    metaData.setFieldU8 (sfTransactionResult, mResult);
    metaData.setFieldU32 (sfTransactionIndex, mIndex);
    metaData.emplace_back (mNodes);
    if (hasDeliveredAmount ())
        metaData.setFieldAmount (sfDeliveredAmount, getDeliveredAmount ());
    return metaData;
}

void TransactionMetaSet::addRaw (Serializer& s, TER result, std::uint32_t index)
{
    mResult = static_cast<int> (result);
    mIndex = index;
    assert ((mResult == 0) || ((mResult > 100) && (mResult <= 255)));

    mNodes.sort (compare);

    getAsObject ().add (s);
}

} // ripple
