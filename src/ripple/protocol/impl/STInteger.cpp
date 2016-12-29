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
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/STInteger.h>
#include <ripple/protocol/TxFormats.h>
#include <ripple/protocol/TER.h>
#include <ripple/beast/core/LexicalCast.h>

namespace ripple {

template<>
STInteger<unsigned char>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get8())
{
}

template <>
SerializedTypeID
STUInt8::getSType () const
{
    return STI_UINT8;
}

template <>
std::string
STUInt8::getText () const
{
    if (getFName () == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo (static_cast<TER> (value_), token, human))
            return human;

        JLOG (debugLog().error())
            << "Unknown result code in metadata: " << value_;
    }

    return beast::lexicalCastThrow <std::string> (value_);
}

template <>
Json::Value
STUInt8::getJson (int) const
{
    if (getFName () == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo (static_cast<TER> (value_), token, human))
            return Json::Value{token};

        JLOG (debugLog().error())
            << "Unknown result code in metadata: " << value_;
    }

    return value_;
}

//------------------------------------------------------------------------------

template<>
STInteger<std::uint16_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get16())
{
}

template <>
SerializedTypeID
STUInt16::getSType () const
{
    return STI_UINT16;
}

template <>
std::string
STUInt16::getText () const
{
    if (getFName () == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance ().findByType (
            static_cast <LedgerEntryType> (value_));

        if (item != nullptr)
            return item->getName ();
    }

    if (getFName () == sfTransactionType)
    {
        auto item =TxFormats::getInstance().findByType (
            static_cast <TxType> (value_));

        if (item != nullptr)
            return item->getName ();
    }

    return beast::lexicalCastThrow <std::string> (value_);
}

template <>
Json::Value
STUInt16::getJson (int) const
{
    if (getFName () == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance ().findByType (
            static_cast <LedgerEntryType> (value_));

        if (item != nullptr)
            return Json::Value{item->getName()};
    }

    if (getFName () == sfTransactionType)
    {
        auto item = TxFormats::getInstance().findByType (
            static_cast <TxType> (value_));

        if (item != nullptr)
            return Json::Value{item->getName()};
    }

    return value_;
}

//------------------------------------------------------------------------------

template<>
STInteger<std::uint32_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get32())
{
}

template <>
SerializedTypeID
STUInt32::getSType () const
{
    return STI_UINT32;
}

template <>
std::string
STUInt32::getText () const
{
    return beast::lexicalCastThrow <std::string> (value_);
}

template <>
Json::Value
STUInt32::getJson (int) const
{
    return value_;
}

//------------------------------------------------------------------------------

template<>
STInteger<std::uint64_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get64())
{
}

template <>
SerializedTypeID
STUInt64::getSType () const
{
    return STI_UINT64;
}

template <>
std::string
STUInt64::getText () const
{
    return beast::lexicalCastThrow <std::string> (value_);
}

template <>
Json::Value
STUInt64::getJson (int) const
{
    return Json::Value{strHex(value_)};
}

} // ripple
