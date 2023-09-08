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

#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/jss.h>
#include <boost/format.hpp>

namespace ripple {

static SOTemplate const&
getSLEFormat(LedgerEntryType type)
{
    if (auto const f = LedgerFormats::getInstance().findByType(type))
        return f->getSOTemplate();

    Throw<std::runtime_error>(
        "SLE (" + std::to_string(safe_cast<std::uint16_t>(type)) +
        "): Unknown format");
}

STLedgerEntry::STLedgerEntry(Keylet const& k)
    : STObject(getSLEFormat(k.type), sfLedgerEntry), key_(k.key), type_(k.type)
{
    setFieldU16(sfLedgerEntryType, static_cast<std::uint16_t>(type_));
}

STLedgerEntry::STLedgerEntry(Slice data, uint256 const& key)
    : STObject(data, sfLedgerEntry)
    , key_(key)
    , type_(safe_cast<LedgerEntryType>(getFieldU16(sfLedgerEntryType)))
{
    applyTemplate(getSLEFormat(type_));  // May throw
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

    return ret;
}

bool
STLedgerEntry::isThreadedType() const
{
    return getFieldIndex(sfPreviousTxnID) != -1;
}

}  // namespace ripple
