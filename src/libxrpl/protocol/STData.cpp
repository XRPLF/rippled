//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/detail/STVar.h>
#include <xrpl/protocol/jss.h>
#include <cstring>
#include <stdexcept>

namespace ripple {

// TODO
STData::STData(SField const& n)
    : STBase(n), inner_type_(STI_NOTPRESENT), data_(STBase{})
{
}

STData::STData(SField const& n, unsigned char v)
    : STBase(n)
    , inner_type_(STI_UINT8)
    , data_(detail::STVar(detail::defaultObject, sfCloseResolution))
{
    setFieldUsingSetValue<STUInt8>(v);
}

STData::STData(SField const& n, std::uint16_t v)
    : STBase(n)
    , inner_type_(STI_UINT16)
    , data_(detail::STVar(detail::defaultObject, sfSignerWeight))
{
    setFieldUsingSetValue<STUInt16>(v);
}

STData::STData(SField const& n, std::uint32_t v)
    : STBase(n)
    , inner_type_(STI_UINT32)
    , data_(detail::STVar(detail::defaultObject, sfNetworkID))
{
    setFieldUsingSetValue<STUInt32>(v);
}

STData::STData(SField const& n, std::uint64_t v)
    : STBase(n)
    , inner_type_(STI_UINT64)
    , data_(detail::STVar(detail::defaultObject, sfIndexNext))
{
    setFieldUsingSetValue<STUInt64>(v);
}

STData::STData(SField const& n, uint128 const& v)
    : STBase(n)
    , inner_type_(STI_UINT128)
    , data_(detail::STVar(detail::defaultObject, sfEmailHash))
{
    setFieldUsingSetValue<STUInt128>(v);
}

STData::STData(SField const& n, uint256 const& v)
    : STBase(n)
    , inner_type_(STI_UINT256)
    , data_(detail::STVar(detail::defaultObject, sfLedgerHash))
{
    setFieldUsingSetValue<STUInt256>(v);
}

STData::STData(SField const& n, Blob const& v)
    : STBase(n)
    , inner_type_(STI_VL)
    , data_(detail::STVar(detail::defaultObject, sfURI))
{
    setFieldUsingSetValue<STBlob>(Buffer(v.data(), v.size()));
}

STData::STData(SField const& n, Slice const& v)
    : STBase(n)
    , inner_type_(STI_VL)
    , data_(detail::STVar(detail::defaultObject, sfURI))
{
    setFieldUsingSetValue<STBlob>(Buffer(v.data(), v.size()));
}

STData::STData(SField const& n, AccountID const& v)
    : STBase(n)
    , inner_type_(STI_ACCOUNT)
    , data_(detail::STVar(detail::defaultObject, sfAccount))
{
    setFieldUsingSetValue<STAccount>(v);
}

STData::STData(SField const& n, STAmount const& v)
    : STBase(n)
    , inner_type_(STI_AMOUNT)
    , data_(detail::STVar(detail::defaultObject, sfAmount))
{
    setFieldUsingAssignment(v);
}

// TODO
STData::STData(SerialIter& sit, SField const& name)
    : STBase(name), data_(STBase{})
{
    std::uint16_t stype = SerializedTypeID(sit.get16());
    inner_type_ = stype;
    SerializedTypeID s = static_cast<SerializedTypeID>(stype);
    switch (s)
    {
        case STI_UINT8: {
            data_ = detail::STVar(sit, sfCloseResolution);
            break;
        }
        case STI_UINT16: {
            data_ = detail::STVar(sit, sfSignerWeight);
            break;
        }
        case STI_UINT32: {
            data_ = detail::STVar(sit, sfNetworkID);
            break;
        }
        case STI_UINT64: {
            data_ = detail::STVar(sit, sfIndexNext);
            break;
        }
        case STI_UINT128: {
            data_ = detail::STVar(sit, sfEmailHash);
            break;
        }
        case STI_UINT256: {
            data_ = detail::STVar(sit, sfLedgerHash);
            break;
        }
        case STI_VL: {
            data_ = detail::STVar(sit, sfURI);
            break;
        }
        case STI_ACCOUNT: {
            data_ = detail::STVar(sit, sfAccount);
            break;
        }
        case STI_AMOUNT: {
            data_ = detail::STVar(sit, sfAmount);
            break;
        }
        default:
            Throw<std::runtime_error>("STData: unknown type");
    }
}

STBase*
STData::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STData::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

std::size_t
STData::size() const
{
    switch (static_cast<SerializedTypeID>(inner_type_))
    {
        case STI_UINT8: {
            return sizeof(uint8_t);
        }
        case STI_UINT16: {
            return sizeof(uint16_t);
        }
        case STI_UINT32: {
            return sizeof(uint32_t);
        }
        case STI_UINT64: {
            return sizeof(uint64_t);
        }
        case STI_UINT128: {
            return uint128::size();
        }
        case STI_UINT256: {
            return uint256::size();
        }
        case STI_VL: {
            const STBlob& st_blob = data_.get().downcast<STBlob>();
            return st_blob.size();
        }
        case STI_ACCOUNT: {
            return uint160::size();
        }
        case STI_AMOUNT: {
            // TODO: STAmount::size()
            const STAmount& st_amt = data_.get().downcast<STAmount>();
            return st_amt.native() ? 8 : 48;
        }
        default:
            Throw<std::runtime_error>("STData: unknown type");
    }
}

SerializedTypeID
STData::getSType() const
{
    return STI_DATA;
}

void
STData::add(Serializer& s) const
{
    // assert(getFName().isBinary());
    // assert(getFName().fieldType == STI_DATA); // TODO: uncomment when
    // STI_DATA is fully integrated
    s.add16(inner_type_);

    switch (static_cast<SerializedTypeID>(inner_type_))
    {
        case STI_UINT8: {
            const STUInt8& st_uint8 = data_.get().downcast<STUInt8>();
            st_uint8.add(s);
            break;
        }
        case STI_UINT16: {
            const STUInt16& st_uint16 = data_.get().downcast<STUInt16>();
            st_uint16.add(s);
            break;
        }
        case STI_UINT32: {
            const STUInt32& st_uint32 = data_.get().downcast<STUInt32>();
            st_uint32.add(s);
            break;
        }
        case STI_UINT64: {
            const STUInt64& st_uint64 = data_.get().downcast<STUInt64>();
            st_uint64.add(s);
            break;
        }
        case STI_UINT128: {
            const STUInt128& st_uint128 = data_.get().downcast<STUInt128>();
            st_uint128.add(s);
            break;
        }
        case STI_UINT256: {
            const STUInt256& st_uint256 = data_.get().downcast<STUInt256>();
            st_uint256.add(s);
            break;
        }
        case STI_VL: {
            const STBlob& st_blob = data_.get().downcast<STBlob>();
            st_blob.add(s);
            break;
        }
        case STI_ACCOUNT: {
            const STAccount& st_acc = data_.get().downcast<STAccount>();
            st_acc.add(s);
            break;
        }
        case STI_AMOUNT: {
            const STAmount& st_amt = data_.get().downcast<STAmount>();
            st_amt.add(s);
            break;
        }
        default:
            Throw<std::runtime_error>("STData: unknown type");
    }
}

bool
STData::isEquivalent(const STBase& t) const
{
    auto const* const tPtr = dynamic_cast<STData const*>(&t);
    return tPtr && (default_ == tPtr->default_) &&
        (inner_type_ == tPtr->inner_type_) && (data_ == tPtr->data_);
}

bool
STData::isDefault() const
{
    return default_;
}

std::string
STData::getInnerTypeString() const
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
        // Add other known types as needed
        default:
            inner_type_str = std::to_string(inner_type_);
    }

    return inner_type_str;
}

std::string
STData::getText() const
{
    std::string inner_type_str = getInnerTypeString();
    return "STData{InnerType: " + inner_type_str +
        ", Data: " + data_.get().getText() + "}";
}

Json::Value
STData::getJson(JsonOptions options) const
{
    Json::Value ret(Json::objectValue);
    ret[jss::type] = getInnerTypeString();
    ret[jss::value] = data_.get().getJson(options);
    return ret;
}

STBase*
STData::makeFieldPresent()
{
    STBase* f = &data_.get();  // getPIndex(index);

    if (f->getSType() != STI_NOTPRESENT)
        return f;

    data_ = detail::STVar(detail::nonPresentObject, f->getFName());
    return &data_.get();
}

void
STData::setFieldU8(unsigned char v)
{
    inner_type_ = STI_UINT8;
    data_ = detail::STVar(detail::defaultObject, sfCloseResolution);
    setFieldUsingSetValue<STUInt8>(v);
}

void
STData::setFieldU16(std::uint16_t v)
{
    inner_type_ = STI_UINT16;
    data_ = detail::STVar(detail::defaultObject, sfSignerWeight);
    setFieldUsingSetValue<STUInt16>(v);
}

void
STData::setFieldU32(std::uint32_t v)
{
    inner_type_ = STI_UINT32;
    data_ = detail::STVar(detail::defaultObject, sfNetworkID);
    setFieldUsingSetValue<STUInt32>(v);
}

void
STData::setFieldU64(std::uint64_t v)
{
    inner_type_ = STI_UINT64;
    data_ = detail::STVar(detail::defaultObject, sfIndexNext);
    setFieldUsingSetValue<STUInt64>(v);
}

void
STData::setFieldH128(uint128 const& v)
{
    inner_type_ = STI_UINT128;
    data_ = detail::STVar(detail::defaultObject, sfEmailHash);
    setFieldUsingSetValue<STUInt128>(v);
}

void
STData::setFieldH256(uint256 const& v)
{
    inner_type_ = STI_UINT256;
    data_ = detail::STVar(detail::defaultObject, sfLedgerHash);
    setFieldUsingSetValue<STUInt256>(v);
}

void
STData::setAccountID(AccountID const& v)
{
    inner_type_ = STI_ACCOUNT;
    data_ = detail::STVar(detail::defaultObject, sfAccount);
    setFieldUsingSetValue<STAccount>(v);
}

void
STData::setFieldVL(Blob const& v)
{
    inner_type_ = STI_VL;
    data_ = detail::STVar(detail::defaultObject, sfData);
    setFieldUsingSetValue<STBlob>(Buffer(v.data(), v.size()));
}

void
STData::setFieldVL(Slice const& s)
{
    inner_type_ = STI_VL;
    data_ = detail::STVar(detail::defaultObject, sfData);
    setFieldUsingSetValue<STBlob>(Buffer(s.data(), s.size()));
}

void
STData::setFieldAmount(STAmount const& v)
{
    inner_type_ = STI_AMOUNT;
    data_ = detail::STVar(detail::defaultObject, sfAmount);
    setFieldUsingAssignment(v);
}

unsigned char
STData::getFieldU8() const
{
    return getFieldByValue<STUInt8>();
}

std::uint16_t
STData::getFieldU16() const
{
    return getFieldByValue<STUInt16>();
}

std::uint32_t
STData::getFieldU32() const
{
    return getFieldByValue<STUInt32>();
}

std::uint64_t
STData::getFieldU64() const
{
    return getFieldByValue<STUInt64>();
}

uint128
STData::getFieldH128() const
{
    return getFieldByValue<STUInt128>();
}

uint160
STData::getFieldH160() const
{
    return getFieldByValue<STUInt160>();
}

uint256
STData::getFieldH256() const
{
    return getFieldByValue<STUInt256>();
}

AccountID
STData::getAccountID() const
{
    return getFieldByValue<STAccount>();
}

Blob
STData::getFieldVL() const
{
    STBlob empty;
    STBlob const& b = getFieldByConstRef<STBlob>(empty);
    return Blob(b.data(), b.data() + b.size());
}

STAmount const&
STData::getFieldAmount() const
{
    static STAmount const empty{};
    return getFieldByConstRef<STAmount>(empty);
}

STData
dataFromJson(SField const& field, Json::Value const& v)
{
    Json::Value type;
    Json::Value value;

    if (!v.isObject())
        Throw<std::runtime_error>("STData: expected object");

    type = v[jss::type];
    value = v[jss::value];

    if (type.isNull())
        Throw<std::runtime_error>("STData: type is null");
    if (value.isNull())
        Throw<std::runtime_error>("STData: value is null");

    auto typeStr = type.asString();

    if (typeStr == "UINT8")
    {
        STData data(field, static_cast<unsigned char>(value.asUInt()));
        return data;
    }
    else if (typeStr == "UINT16")
    {
        STData data(field, static_cast<std::uint16_t>(value.asUInt()));
        return data;
    }
    else if (typeStr == "UINT32")
    {
        STData data(field, static_cast<std::uint32_t>(value.asUInt()));
        return data;
    }
    else if (typeStr == "UINT64")
    {
        STData data(field, static_cast<std::uint64_t>(value.asUInt()));
        return data;
    }
    else if (typeStr == "UINT128")
    {
        STData data(field, static_cast<uint128>(value.asUInt()));
        return data;
    }
    else if (typeStr == "UINT256")
    {
        STData data(field, static_cast<uint256>(value.asUInt()));
        return data;
    }
    else if (typeStr == "VL")
    {
        auto vBlob = strUnHex(value.asString());
        if (vBlob.has_value())
        {
            STData data(field, vBlob.value());
            return data;
        }
        Throw<std::invalid_argument>("invalid data");
    }
    else if (typeStr == "ACCOUNT")
    {
        AccountID issuer;
        if (!to_issuer(issuer, value.asString()))
            Throw<std::runtime_error>("STData: invalid account");
        STData data(field, issuer);
        return data;
    }
    else if (typeStr == "AMOUNT")
    {
        STData data(field, amountFromJson(field, value));
        return data;
    }

    // Handle unknown or unsupported type
    Throw<std::runtime_error>("STData: unsupported type string: " + typeStr);
}

}  // namespace ripple
