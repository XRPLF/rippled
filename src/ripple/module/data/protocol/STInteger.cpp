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

#include <ripple/module/data/protocol/STInteger.h>

namespace ripple {

template <>
SerializedTypeID STUInt8::getSType () const
{
    return STI_UINT8;
}

template <>
STUInt8* STUInt8::construct (SerializerIterator& u, SField::ref name)
{
    return new STUInt8 (name, u.get8 ());
}

template <>
std::string STUInt8::getText () const
{
    if (getFName () == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo (static_cast<TER> (value_), token, human))
            return human;
    }

    return std::to_string (value_);
}

template <>
Json::Value STUInt8::getJson (int) const
{
    if (getFName () == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo (static_cast<TER> (value_), token, human))
            return token;
        else
            WriteLog (lsWARNING, SerializedType)
                << "Unknown result code in metadata: " << value_;
    }

    return value_;
}

//------------------------------------------------------------------------------

template <>
SerializedTypeID STUInt16::getSType () const
{
    return STI_UINT16;
}

template <>
STUInt16* STUInt16::construct (SerializerIterator& u, SField::ref name)
{
    return new STUInt16 (name, u.get16 ());
}

template <>
std::string STUInt16::getText () const
{
    if (getFName () == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance ()->findByType (
            static_cast <LedgerEntryType> (value_));

        if (item != nullptr)
            return item->getName ();
    }

    if (getFName () == sfTransactionType)
    {
        TxFormats::Item const* const item =
            TxFormats::getInstance()->findByType (static_cast <TxType> (value_));

        if (item != nullptr)
            return item->getName ();
    }

    return std::to_string (value_);
}

template <>
Json::Value STUInt16::getJson (int) const
{
    if (getFName () == sfLedgerEntryType)
    {
        LedgerFormats::Item const* const item =
            LedgerFormats::getInstance ()->findByType (static_cast <LedgerEntryType> (value_));

        if (item != nullptr)
            return item->getName ();
    }

    if (getFName () == sfTransactionType)
    {
        TxFormats::Item const* const item =
            TxFormats::getInstance()->findByType (static_cast <TxType> (value_));

        if (item != nullptr)
            return item->getName ();
    }

    return value_;
}

//------------------------------------------------------------------------------

template <>
SerializedTypeID STUInt32::getSType () const
{
    return STI_UINT32;
}

template <>
STUInt32* STUInt32::construct (SerializerIterator& u, SField::ref name)
{
    return new STUInt32 (name, u.get32 ());
}

template <>
std::string STUInt32::getText () const
{
    return std::to_string (value_);
}

template <>
Json::Value STUInt32::getJson (int) const
{
    return value_;
}

template <>
SerializedTypeID STUInt64::getSType () const
{
    return STI_UINT64;
}

template <>
STUInt64* STUInt64::construct (SerializerIterator& u, SField::ref name)
{
    return new STUInt64 (name, u.get64 ());
}

template <>
std::string STUInt64::getText () const
{
    return std::to_string (value_);
}

template <>
Json::Value STUInt64::getJson (int) const
{
    return strHex (value_);
}

} // ripple
