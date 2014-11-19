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

#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STLedgerEntry.h>

namespace ripple {

STLedgerEntry::STLedgerEntry (
    SerializerIterator& sit, uint256 const& index)
    : STObject (sfLedgerEntry), mIndex (index), mMutable (true)
{
    set (sit);
    setSLEType ();
}

STLedgerEntry::STLedgerEntry (
    const Serializer& s, uint256 const& index)
    : STObject (sfLedgerEntry), mIndex (index), mMutable (true)
{
    // we know 's' isn't going away
    SerializerIterator sit (const_cast<Serializer&> (s));
    set (sit);
    setSLEType ();
}

STLedgerEntry::STLedgerEntry (
    const STObject & object, uint256 const& index)
    : STObject (object), mIndex(index),  mMutable (true)
{
    setSLEType ();
}

void STLedgerEntry::setSLEType ()
{
    mFormat = LedgerFormats::getInstance().findByType (
        static_cast <LedgerEntryType> (getFieldU16 (sfLedgerEntryType)));

    if (mFormat == nullptr)
        throw std::runtime_error ("invalid ledger entry type");

    mType = mFormat->getType ();
    if (!setType (mFormat->elements))
    {
        WriteLog (lsWARNING, SerializedLedger)
            << "Ledger entry not valid for type " << mFormat->getName ();
        WriteLog (lsWARNING, SerializedLedger) << getJson (0);
        throw std::runtime_error ("ledger entry not valid for type");
    }
}

STLedgerEntry::STLedgerEntry (LedgerEntryType type, uint256 const& index) :
    STObject (sfLedgerEntry), mIndex (index), mType (type), mMutable (true)
{
    mFormat = LedgerFormats::getInstance().findByType (type);

    if (mFormat == nullptr)
        throw std::runtime_error ("invalid ledger entry type");

    set (mFormat->elements);
    setFieldU16 (sfLedgerEntryType,
        static_cast <std::uint16_t> (mFormat->getType ()));
}

STLedgerEntry::pointer STLedgerEntry::getMutable () const
{
    STLedgerEntry::pointer ret = std::make_shared<STLedgerEntry> (std::cref (*this));
    ret->mMutable = true;
    return ret;
}

std::string STLedgerEntry::getFullText () const
{
    std::string ret = "\"";
    ret += to_string (mIndex);
    ret += "\" = { ";
    ret += mFormat->getName ();
    ret += ", ";
    ret += STObject::getFullText ();
    ret += "}";
    return ret;
}

std::string STLedgerEntry::getText () const
{
    return str (boost::format ("{ %s, %s }")
                % to_string (mIndex)
                % STObject::getText ());
}

Json::Value STLedgerEntry::getJson (int options) const
{
    Json::Value ret (STObject::getJson (options));

    ret["index"] = to_string (mIndex);

    return ret;
}

bool STLedgerEntry::isThreadedType ()
{
    return getFieldIndex (sfPreviousTxnID) != -1;
}

bool STLedgerEntry::isThreaded ()
{
    return isFieldPresent (sfPreviousTxnID);
}

uint256 STLedgerEntry::getThreadedTransaction ()
{
    return getFieldH256 (sfPreviousTxnID);
}

std::uint32_t STLedgerEntry::getThreadedLedger ()
{
    return getFieldU32 (sfPreviousTxnLgrSeq);
}

bool STLedgerEntry::thread (uint256 const& txID, std::uint32_t ledgerSeq,
                                    uint256& prevTxID, std::uint32_t& prevLedgerID)
{
    uint256 oldPrevTxID = getFieldH256 (sfPreviousTxnID);
    WriteLog (lsTRACE, SerializedLedger) << "Thread Tx:" << txID << " prev:" << oldPrevTxID;

    if (oldPrevTxID == txID)
    {
        // this transaction is already threaded
        assert (getFieldU32 (sfPreviousTxnLgrSeq) == ledgerSeq);
        return false;
    }

    prevTxID = oldPrevTxID;
    prevLedgerID = getFieldU32 (sfPreviousTxnLgrSeq);
    setFieldH256 (sfPreviousTxnID, txID);
    setFieldU32 (sfPreviousTxnLgrSeq, ledgerSeq);
    return true;
}

bool STLedgerEntry::hasOneOwner ()
{
    return (mType != ltACCOUNT_ROOT) && (getFieldIndex (sfAccount) != -1);
}

bool STLedgerEntry::hasTwoOwners ()
{
    return mType == ltRIPPLE_STATE;
}

RippleAddress STLedgerEntry::getOwner ()
{
    return getFieldAccount (sfAccount);
}

RippleAddress STLedgerEntry::getFirstOwner ()
{
    return RippleAddress::createAccountID (getFieldAmount (sfLowLimit).getIssuer ());
}

RippleAddress STLedgerEntry::getSecondOwner ()
{
    return RippleAddress::createAccountID (getFieldAmount (sfHighLimit).getIssuer ());
}

std::vector<uint256> STLedgerEntry::getOwners ()
{
    std::vector<uint256> owners;
    Account account;

    for (int i = 0, fields = getCount (); i < fields; ++i)
    {
        auto const& fc = getFieldSType (i);

        if ((fc == sfAccount) || (fc == sfOwner))
        {
            auto entry = dynamic_cast<const STAccount*> (peekAtPIndex (i));

            if ((entry != nullptr) && entry->getValueH160 (account))
                owners.push_back (getAccountRootIndex (account));
        }

        if ((fc == sfLowLimit) || (fc == sfHighLimit))
        {
            auto entry = dynamic_cast<const STAmount*> (peekAtPIndex (i));

            if ((entry != nullptr))
            {
                auto issuer = entry->getIssuer ();

                if (issuer.isNonZero ())
                    owners.push_back (getAccountRootIndex (issuer));
            }
        }
    }

    return owners;
}

} // ripple
