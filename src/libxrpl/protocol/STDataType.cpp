//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STDataType.h>
#include <xrpl/protocol/detail/STVar.h>
#include <xrpl/protocol/jss.h>

#include <cstring>
#include <stdexcept>

namespace ripple {

// TODO
STDataType::STDataType(SField const& n) : STBase(n), inner_type_(STI_NOTPRESENT)
{
}

STDataType::STDataType(SField const& n, SerializedTypeID v)
    : STBase(n), inner_type_(v), default_(false)
{
}

STDataType::STDataType(SerialIter& sit, SField const& name)
    : STBase(name), inner_type_(STI_DATA), default_(false)
{
    std::uint16_t stype = SerializedTypeID(sit.get16());
    inner_type_ = stype;
}

STBase*
STDataType::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STDataType::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STDataType::getSType() const
{
    return STI_DATATYPE;
}

void
STDataType::setInnerSType(SerializedTypeID v)
{
    inner_type_ = v;
}

void
STDataType::add(Serializer& s) const
{
    s.add16(inner_type_);
}

bool
STDataType::isEquivalent(STBase const& t) const
{
    auto const* const tPtr = dynamic_cast<STDataType const*>(&t);
    return tPtr && (default_ == tPtr->default_) &&
        (inner_type_ == tPtr->inner_type_);
}

bool
STDataType::isDefault() const
{
    return default_;
}

std::string
STDataType::getInnerTypeString() const
{
    std::string inner_type_str = "Unknown";
    // Optionally, convert inner_type_ to its string representation if mappings
    // exist
    switch (static_cast<SerializedTypeID>(inner_type_))
    {
        case STI_UINT8:
            inner_type_str = "UINT8";
            break;
        case STI_UINT16:
            inner_type_str = "UINT16";
            break;
        case STI_UINT32:
            inner_type_str = "UINT32";
            break;
        case STI_UINT64:
            inner_type_str = "UINT64";
            break;
        case STI_UINT128:
            inner_type_str = "UINT128";
            break;
        case STI_UINT160:
            inner_type_str = "UINT160";
            break;
        case STI_UINT192:
            inner_type_str = "UINT192";
            break;
        case STI_UINT256:
            inner_type_str = "UINT256";
            break;
        case STI_VL:
            inner_type_str = "VL";
            break;
        case STI_ACCOUNT:
            inner_type_str = "ACCOUNT";
            break;
        case STI_AMOUNT:
            inner_type_str = "AMOUNT";
            break;
        case STI_ISSUE:
            inner_type_str = "ISSUE";
            break;
        case STI_CURRENCY:
            inner_type_str = "CURRENCY";
            break;
        case STI_NUMBER:
            inner_type_str = "NUMBER";
            break;
        // Add other known types as needed
        default:
            inner_type_str = std::to_string(inner_type_);
    }

    return inner_type_str;
}

std::string
STDataType::getText() const
{
    std::string inner_type_str = getInnerTypeString();
    return "STDataType{InnerType: " + inner_type_str + "}";
}

Json::Value
STDataType::getJson(JsonOptions) const
{
    Json::Value ret(Json::objectValue);
    ret[jss::type] = getInnerTypeString();
    return ret;
}

STDataType
dataTypeFromJson(SField const& field, Json::Value const& v)
{
    SerializedTypeID typeId = STI_NOTPRESENT;
    Json::Value type;
    Json::Value value;

    if (!v.isObject())
    {
        Throw<std::runtime_error>("STData: expected object");
    }

    type = v[jss::type];
    auto typeStr = type.asString();

    if (typeStr == "UINT8")
    {
        typeId = STI_UINT8;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "UINT16")
    {
        typeId = STI_UINT16;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "UINT32")
    {
        typeId = STI_UINT32;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "UINT64")
    {
        typeId = STI_UINT64;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "UINT128")
    {
        typeId = STI_UINT128;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "UINT160")
    {
        typeId = STI_UINT160;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "UINT192")
    {
        typeId = STI_UINT192;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "UINT256")
    {
        typeId = STI_UINT256;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "VL")
    {
        typeId = STI_VL;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "ACCOUNT")
    {
        typeId = STI_ACCOUNT;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "AMOUNT")
    {
        typeId = STI_AMOUNT;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "ISSUE")
    {
        typeId = STI_ISSUE;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "CURRENCY")
    {
        typeId = STI_CURRENCY;
        STDataType data(field, typeId);
        return data;
    }
    else if (typeStr == "NUMBER")
    {
        typeId = STI_NUMBER;
        STDataType data(field, typeId);
        return data;
    }

    // Handle unknown or unsupported type
    Throw<std::runtime_error>("STData: unsupported type string: " + typeStr);
}

}  // namespace ripple
