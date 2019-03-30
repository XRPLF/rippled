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

#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/jss.h>
#include <boost/format.hpp>

namespace ripple {

STLedgerEntry::STLedgerEntry (Keylet const& k)
    :  STObject(sfLedgerEntry)
    , key_ (k.key)
    , type_ (k.type)
{
    if (!(0u <= type_ &&
        type_ <= std::min<unsigned>(std::numeric_limits<std::uint16_t>::max(),
        std::numeric_limits<std::underlying_type_t<LedgerEntryType>>::max())))
            Throw<std::runtime_error> ("invalid ledger entry type: out of range");
    auto const format =
        LedgerFormats::getInstance().findByType (type_);

    if (format == nullptr)
        Throw<std::runtime_error> ("invalid ledger entry type");

    set (format->getSOTemplate());

    setFieldU16 (sfLedgerEntryType,
        static_cast <std::uint16_t> (type_));
}

STLedgerEntry::STLedgerEntry (
        SerialIter& sit,
        uint256 const& index)
    : STObject (sfLedgerEntry)
    , key_ (index)
{
    set (sit);
    setSLEType ();
}

STLedgerEntry::STLedgerEntry (
        STObject const& object,
        uint256 const& index)
    : STObject (object)
    , key_ (index)
{
    setSLEType ();
}

void STLedgerEntry::setSLEType ()
{
    auto format = LedgerFormats::getInstance().findByType (
        safe_cast <LedgerEntryType> (
            getFieldU16 (sfLedgerEntryType)));

    if (format == nullptr)
        Throw<std::runtime_error> ("invalid ledger entry type");

    type_ = format->getType ();
    applyTemplate (format->getSOTemplate());  // May throw
}

std::string STLedgerEntry::getFullText () const
{
    auto const format =
        LedgerFormats::getInstance().findByType (type_);

    if (format == nullptr)
        Throw<std::runtime_error> ("invalid ledger entry type");

    std::string ret = "\"";
    ret += to_string (key_);
    ret += "\" = { ";
    ret += format->getName ();
    ret += ", ";
    ret += STObject::getFullText ();
    ret += "}";
    return ret;
}

std::string STLedgerEntry::getText () const
{
    return str (boost::format ("{ %s, %s }")
                % to_string (key_)
                % STObject::getText ());
}

Json::Value STLedgerEntry::getJson (int options) const
{
    Json::Value ret (STObject::getJson (options));

    ret[jss::index] = to_string (key_);

    return ret;
}

bool STLedgerEntry::isThreadedType () const
{
    return getFieldIndex (sfPreviousTxnID) != -1;
}

bool STLedgerEntry::thread (
    uint256 const& txID,
    std::uint32_t ledgerSeq,
    uint256& prevTxID,
    std::uint32_t& prevLedgerID)
{
    uint256 oldPrevTxID = getFieldH256 (sfPreviousTxnID);

    JLOG (debugLog().info())
        << "Thread Tx:" << txID << " prev:" << oldPrevTxID;

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

} // ripple
