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
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <charconv>

namespace ripple {

template <>
STInteger<unsigned char>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get8())
{
}

template <>
SerializedTypeID
STUInt8::getSType() const
{
    return STI_UINT8;
}

template <>
std::string
STUInt8::getText() const
{
    if (getFName() == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo(TER::fromInt(value_), token, human))
            return human;

        JLOG(debugLog().error())
            << "Unknown result code in metadata: " << value_;
    }

    return std::to_string(value_);
}

template <>
Json::Value
STUInt8::getJson(JsonOptions) const
{
    if (getFName() == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo(TER::fromInt(value_), token, human))
            return token;

        JLOG(debugLog().error())
            << "Unknown result code in metadata: " << value_;
    }

    return value_;
}

//------------------------------------------------------------------------------

template <>
STInteger<std::uint16_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get16())
{
}

template <>
SerializedTypeID
STUInt16::getSType() const
{
    return STI_UINT16;
}

template <>
std::string
STUInt16::getText() const
{
    if (getFName() == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance().findByType(
            safe_cast<LedgerEntryType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    if (getFName() == sfTransactionType)
    {
        auto item =
            TxFormats::getInstance().findByType(safe_cast<TxType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    return std::to_string(value_);
}

template <>
Json::Value
STUInt16::getJson(JsonOptions) const
{
    if (getFName() == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance().findByType(
            safe_cast<LedgerEntryType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    if (getFName() == sfTransactionType)
    {
        auto item =
            TxFormats::getInstance().findByType(safe_cast<TxType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    return value_;
}

//------------------------------------------------------------------------------

template <>
STInteger<std::uint32_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get32())
{
}

template <>
SerializedTypeID
STUInt32::getSType() const
{
    return STI_UINT32;
}

template <>
std::string
STUInt32::getText() const
{
    return std::to_string(value_);
}

template <>
Json::Value
STUInt32::getJson(JsonOptions) const
{
    return value_;
}

//------------------------------------------------------------------------------

template <>
STInteger<std::uint64_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get64())
{
}

template <>
SerializedTypeID
STUInt64::getSType() const
{
    return STI_UINT64;
}

template <>
std::string
STUInt64::getText() const
{
    return std::to_string(value_);
}

template <>
Json::Value
STUInt64::getJson(JsonOptions) const
{
    auto convertToString = [](uint64_t const value, int const base) {
        ASSERT(
            base == 10 || base == 16,
            "ripple::STUInt64::getJson : base 10 or 16");
        std::string str(
            base == 10 ? 20 : 16, 0);  // Allocate space depending on base
        auto ret =
            std::to_chars(str.data(), str.data() + str.size(), value, base);
        ASSERT(
            ret.ec == std::errc(),
            "ripple::STUInt64::getJson : to_chars succeeded");
        str.resize(std::distance(str.data(), ret.ptr));
        return str;
    };

    if (auto const& fName = getFName(); fName.shouldMeta(SField::sMD_BaseTen))
    {
        return convertToString(value_, 10);  // Convert to base 10
    }

    return convertToString(value_, 16);  // Convert to base 16
}

}  // namespace ripple
