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

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

#include <boost/format/free_funcs.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace ripple {

STLedgerEntry::STLedgerEntry(Keylet const& k)
    : STObject(sfLedgerEntry), key_(k.key), type_(k.type)
{
    auto const format = LedgerFormats::getInstance().findByType(type_);

    if (format == nullptr)
        Throw<std::runtime_error>(
            "Attempt to create a SLE of unknown type " +
            std::to_string(safe_cast<std::uint16_t>(k.type)));

    set(format->getSOTemplate());

    setFieldU16(sfLedgerEntryType, static_cast<std::uint16_t>(type_));
}

STLedgerEntry::STLedgerEntry(SerialIter& sit, uint256 const& index)
    : STObject(sfLedgerEntry), key_(index)
{
    set(sit);
    setSLEType();
}

STLedgerEntry::STLedgerEntry(STObject const& object, uint256 const& index)
    : STObject(object), key_(index)
{
    setSLEType();
}

void
STLedgerEntry::setSLEType()
{
    auto format = LedgerFormats::getInstance().findByType(
        safe_cast<LedgerEntryType>(getFieldU16(sfLedgerEntryType)));

    if (format == nullptr)
        Throw<std::runtime_error>("invalid ledger entry type");

    type_ = format->getType();
    applyTemplate(format->getSOTemplate());  // May throw
}

std::string
STLedgerEntry::getFullText() const
{
    auto const format = LedgerFormats::getInstance().findByType(type_);

    if (format == nullptr)
        Throw<std::runtime_error>("invalid ledger entry type");

    std::string ret = "\"";
    ret += to_string(key_);
    ret += "\" = { ";
    ret += format->getName();
    ret += ", ";
    ret += STObject::getFullText();
    ret += "}";
    return ret;
}

STBase*
STLedgerEntry::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STLedgerEntry::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STLedgerEntry::getSType() const
{
    return STI_LEDGERENTRY;
}

std::string
STLedgerEntry::getText() const
{
    return str(
        boost::format("{ %s, %s }") % to_string(key_) % STObject::getText());
}

Json::Value
STLedgerEntry::getJson(JsonOptions options) const
{
    Json::Value ret(STObject::getJson(options));

    ret[jss::index] = to_string(key_);

    if (getType() == ltMPTOKEN_ISSUANCE)
        ret[jss::mpt_issuance_id] = to_string(
            makeMptID(getFieldU32(sfSequence), getAccountID(sfIssuer)));

    return ret;
}

bool
STLedgerEntry::isThreadedType(Rules const& rules) const
{
    static constexpr std::array<LedgerEntryType, 5> newPreviousTxnIDTypes = {
        ltDIR_NODE, ltAMENDMENTS, ltFEE_SETTINGS, ltNEGATIVE_UNL, ltAMM};
    // Exclude PrevTxnID/PrevTxnLgrSeq if the fixPreviousTxnID amendment is not
    // enabled and the ledger object type is in the above set
    bool const excludePrevTxnID = !rules.enabled(fixPreviousTxnID) &&
        std::count(
            newPreviousTxnIDTypes.cbegin(),
            newPreviousTxnIDTypes.cend(),
            type_);
    return !excludePrevTxnID && getFieldIndex(sfPreviousTxnID) != -1;
}

bool
STLedgerEntry::thread(
    uint256 const& txID,
    std::uint32_t ledgerSeq,
    uint256& prevTxID,
    std::uint32_t& prevLedgerID)
{
    uint256 oldPrevTxID = getFieldH256(sfPreviousTxnID);

    JLOG(debugLog().info()) << "Thread Tx:" << txID << " prev:" << oldPrevTxID;

    if (oldPrevTxID == txID)
    {
        // this transaction is already threaded
        XRPL_ASSERT(
            getFieldU32(sfPreviousTxnLgrSeq) == ledgerSeq,
            "ripple::STLedgerEntry::thread : ledger sequence match");
        return false;
    }

    prevTxID = oldPrevTxID;
    prevLedgerID = getFieldU32(sfPreviousTxnLgrSeq);
    setFieldH256(sfPreviousTxnID, txID);
    setFieldU32(sfPreviousTxnLgrSeq, ledgerSeq);
    return true;
}

}  // namespace ripple
