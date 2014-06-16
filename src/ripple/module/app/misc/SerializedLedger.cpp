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

namespace ripple {

struct SerializedLedgerLog; // for Log

SETUP_LOGN (SerializedLedgerLog,"SerializedLedger")

SerializedLedgerEntry::SerializedLedgerEntry (
    SerializerIterator& sit, uint256 const& index)
    : STObject (sfLedgerEntry), mIndex (index), mMutable (true)
{
    set (sit);
    setSLEType ();
}

SerializedLedgerEntry::SerializedLedgerEntry (
    const Serializer& s, uint256 const& index)
    : STObject (sfLedgerEntry), mIndex (index), mMutable (true)
{
    // we know 's' isn't going away
    SerializerIterator sit (const_cast<Serializer&> (s));
    set (sit);
    setSLEType ();
}

SerializedLedgerEntry::SerializedLedgerEntry (
    const STObject & object, uint256 const& index)
    : STObject (object), mIndex(index),  mMutable (true)
{
    setSLEType ();
}

void SerializedLedgerEntry::setSLEType ()
{
    auto type = static_cast <LedgerEntryType> (getFieldU16 (sfLedgerEntryType));
    auto const item = LedgerFormats::getInstance()->findByType (type);

    if (item == nullptr)
        throw std::runtime_error ("invalid ledger entry type");

    mType = item->getType ();
    if (!setType (item->elements))
    {
        WriteLog (lsWARNING, SerializedLedgerLog)
            << "Ledger entry not valid for type " << mFormat->getName ();
        WriteLog (lsWARNING, SerializedLedgerLog) << getJson (0);
        throw std::runtime_error ("ledger entry not valid for type");
    }
}

SerializedLedgerEntry::SerializedLedgerEntry (LedgerEntryType type, uint256 const& index) :
    STObject (sfLedgerEntry), mIndex (index), mType (type), mMutable (true)
{
    LedgerFormats::Item const* const item =
        LedgerFormats::getInstance()->findByType (type);

    if (item != nullptr)
    {
        set (item->elements);

        setFieldU16 (sfLedgerEntryType, static_cast <std::uint16_t> (item->getType ()));
    }
    else
    {
        throw std::runtime_error ("invalid ledger entry type");
    }
}

SerializedLedgerEntry::pointer SerializedLedgerEntry::getMutable () const
{
    SerializedLedgerEntry::pointer ret = std::make_shared<SerializedLedgerEntry> (std::cref (*this));
    ret->mMutable = true;
    return ret;
}

std::string SerializedLedgerEntry::getFullText () const
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

std::string SerializedLedgerEntry::getText () const
{
    return str (boost::format ("{ %s, %s }")
                % to_string (mIndex)
                % STObject::getText ());
}

Json::Value SerializedLedgerEntry::getJson (int options) const
{
    Json::Value ret (STObject::getJson (options));

    ret["index"] = to_string (mIndex);

    return ret;
}

bool SerializedLedgerEntry::isThreadedType ()
{
    return getFieldIndex (sfPreviousTxnID) != -1;
}

bool SerializedLedgerEntry::isThreaded ()
{
    return isFieldPresent (sfPreviousTxnID);
}

uint256 SerializedLedgerEntry::getThreadedTransaction ()
{
    return getFieldH256 (sfPreviousTxnID);
}

std::uint32_t SerializedLedgerEntry::getThreadedLedger ()
{
    return getFieldU32 (sfPreviousTxnLgrSeq);
}

bool SerializedLedgerEntry::thread (uint256 const& txID, std::uint32_t ledgerSeq,
                                    uint256& prevTxID, std::uint32_t& prevLedgerID)
{
    uint256 oldPrevTxID = getFieldH256 (sfPreviousTxnID);
    WriteLog (lsTRACE, SerializedLedgerLog) << "Thread Tx:" << txID << " prev:" << oldPrevTxID;

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

bool SerializedLedgerEntry::hasOneOwner ()
{
    return (mType != ltACCOUNT_ROOT) && (getFieldIndex (sfAccount) != -1);
}

bool SerializedLedgerEntry::hasTwoOwners ()
{
    return mType == ltRIPPLE_STATE;
}

RippleAddress SerializedLedgerEntry::getOwner ()
{
    return getFieldAccount (sfAccount);
}

RippleAddress SerializedLedgerEntry::getFirstOwner ()
{
    return RippleAddress::createAccountID (getFieldAmount (sfLowLimit).getIssuer ());
}

RippleAddress SerializedLedgerEntry::getSecondOwner ()
{
    return RippleAddress::createAccountID (getFieldAmount (sfHighLimit).getIssuer ());
}

std::vector<uint256> SerializedLedgerEntry::getOwners ()
{
    std::vector<uint256> owners;
    uint160 account;

    for (int i = 0, fields = getCount (); i < fields; ++i)
    {
        SField::ref fc = getFieldSType (i);

        if ((fc == sfAccount) || (fc == sfOwner))
        {
            const STAccount* entry = dynamic_cast<const STAccount*> (peekAtPIndex (i));

            if ((entry != nullptr) && entry->getValueH160 (account))
                owners.push_back (Ledger::getAccountRootIndex (account));
        }

        if ((fc == sfLowLimit) || (fc == sfHighLimit))
        {
            const STAmount* entry = dynamic_cast<const STAmount*> (peekAtPIndex (i));

            if ((entry != nullptr))
            {
                uint160 issuer = entry->getIssuer ();

                if (issuer.isNonZero ())
                    owners.push_back (Ledger::getAccountRootIndex (issuer));
            }
        }
    }

    return owners;
}

} // ripple
