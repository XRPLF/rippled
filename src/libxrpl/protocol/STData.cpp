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
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/detail/STVar.h>
#include <xrpl/protocol/jss.h>

#include <cstring>
#include <stdexcept>

namespace ripple {

template <typename U, typename S>
constexpr std::
    enable_if_t<std::is_unsigned<U>::value && std::is_signed<S>::value, U>
    to_unsigned(S value)
{
    if (value < 0 || std::numeric_limits<U>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U>(value);
}

template <typename U1, typename U2>
constexpr std::
    enable_if_t<std::is_unsigned<U1>::value && std::is_unsigned<U2>::value, U1>
    to_unsigned(U2 value)
{
    if (std::numeric_limits<U1>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U1>(value);
}

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

STData::STData(SField const& n, uint160 const& v)
    : STBase(n)
    , inner_type_(STI_UINT160)
    , data_(detail::STVar(detail::defaultObject, sfTakerPaysCurrency))
{
    setFieldUsingSetValue<STUInt160>(v);
}

STData::STData(SField const& n, uint192 const& v)
    : STBase(n)
    , inner_type_(STI_UINT192)
    , data_(detail::STVar(detail::defaultObject, sfMPTokenIssuanceID))
{
    setFieldUsingSetValue<STUInt192>(v);
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

STData::STData(SField const& n, STAmount const& v)
    : STBase(n)
    , inner_type_(STI_AMOUNT)
    , data_(detail::STVar(detail::defaultObject, sfAmount))
{
    setFieldUsingAssignment(v);
}

STData::STData(SField const& n, AccountID const& v)
    : STBase(n)
    , inner_type_(STI_ACCOUNT)
    , data_(detail::STVar(detail::defaultObject, sfAccount))
{
    setFieldUsingSetValue<STAccount>(v);
}

STData::STData(SField const& n, STIssue const& v)
    : STBase(n)
    , inner_type_(STI_ISSUE)
    , data_(detail::STVar(detail::defaultObject, sfAsset))
{
    setFieldUsingAssignment(v);
}

STData::STData(SField const& n, STCurrency const& v)
    : STBase(n)
    , inner_type_(STI_CURRENCY)
    , data_(detail::STVar(detail::defaultObject, sfBaseAsset))
{
    setFieldUsingAssignment(v);
}

STData::STData(SField const& n, STNumber const& v)
    : STBase(n)
    , inner_type_(STI_NUMBER)
    , data_(detail::STVar(detail::defaultObject, sfNumber))
{
    setFieldUsingAssignment(v);
}

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
        case STI_UINT160: {
            data_ = detail::STVar(sit, sfTakerPaysCurrency);
            break;
        }
        case STI_UINT192: {
            data_ = detail::STVar(sit, sfMPTokenIssuanceID);
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
        case STI_AMOUNT: {
            data_ = detail::STVar(sit, sfAmount);
            break;
        }
        case STI_ACCOUNT: {
            data_ = detail::STVar(sit, sfAccount);
            break;
        }
        case STI_ISSUE: {
            data_ = detail::STVar(sit, sfAsset);
            break;
        }
        case STI_CURRENCY: {
            data_ = detail::STVar(sit, sfBaseAsset);
            break;
        }
        case STI_NUMBER: {
            data_ = detail::STVar(sit, sfNumber);
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
        case STI_UINT160: {
            return uint160::size();
        }
        case STI_UINT192: {
            return uint192::size();
        }
        case STI_UINT256: {
            return uint256::size();
        }
        case STI_VL: {
            STBlob const& st_blob = data_.get().downcast<STBlob>();
            return st_blob.size();
        }
        case STI_AMOUNT: {
            // TODO: STAmount::size()
            STAmount const& st_amt = data_.get().downcast<STAmount>();
            return st_amt.native() ? 8 : 48;
        }
        case STI_ACCOUNT: {
            return uint160::size();
        }
        case STI_ISSUE: {
            // const STIssue& st_issue = data_.get().downcast<STIssue>();
            return 40;  // 20 bytes for currency + 20 bytes for account
        }
        case STI_CURRENCY: {
            // const STCurrency& st_currency =
            // data_.get().downcast<STCurrency>();
            return 20;  // 20 bytes for currency
        }
        case STI_NUMBER: {
            return sizeof(double);
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
    s.add16(inner_type_);

    switch (static_cast<SerializedTypeID>(inner_type_))
    {
        case STI_UINT8: {
            STUInt8 const& st_uint8 = data_.get().downcast<STUInt8>();
            st_uint8.add(s);
            break;
        }
        case STI_UINT16: {
            STUInt16 const& st_uint16 = data_.get().downcast<STUInt16>();
            st_uint16.add(s);
            break;
        }
        case STI_UINT32: {
            STUInt32 const& st_uint32 = data_.get().downcast<STUInt32>();
            st_uint32.add(s);
            break;
        }
        case STI_UINT64: {
            STUInt64 const& st_uint64 = data_.get().downcast<STUInt64>();
            st_uint64.add(s);
            break;
        }
        case STI_UINT128: {
            STUInt128 const& st_uint128 = data_.get().downcast<STUInt128>();
            st_uint128.add(s);
            break;
        }
        case STI_UINT160: {
            STUInt160 const& st_uint160 = data_.get().downcast<STUInt160>();
            st_uint160.add(s);
            break;
        }
        case STI_UINT192: {
            STUInt192 const& st_uint192 = data_.get().downcast<STUInt192>();
            st_uint192.add(s);
            break;
        }
        case STI_UINT256: {
            STUInt256 const& st_uint256 = data_.get().downcast<STUInt256>();
            st_uint256.add(s);
            break;
        }
        case STI_VL: {
            STBlob const& st_blob = data_.get().downcast<STBlob>();
            st_blob.add(s);
            break;
        }
        case STI_AMOUNT: {
            STAmount const& st_amt = data_.get().downcast<STAmount>();
            st_amt.add(s);
            break;
        }
        case STI_ACCOUNT: {
            STAccount const& st_acc = data_.get().downcast<STAccount>();
            st_acc.add(s);
            break;
        }
        case STI_ISSUE: {
            STIssue const& st_issue = data_.get().downcast<STIssue>();
            st_issue.add(s);
            break;
        }
        case STI_CURRENCY: {
            STCurrency const& st_currency = data_.get().downcast<STCurrency>();
            st_currency.add(s);
            break;
        }
        case STI_NUMBER: {
            STNumber const& st_number = data_.get().downcast<STNumber>();
            st_number.add(s);
            break;
        }
        default:
            Throw<std::runtime_error>("STData: unknown type");
    }
}

bool
STData::isEquivalent(STBase const& t) const
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
        case STI_AMOUNT:
            inner_type_str = "AMOUNT";
            break;
        case STI_ACCOUNT:
            inner_type_str = "ACCOUNT";
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
STData::setFieldH160(uint160 const& v)
{
    inner_type_ = STI_UINT160;
    data_ = detail::STVar(detail::defaultObject, sfTakerPaysCurrency);
    setFieldUsingSetValue<STUInt160>(v);
}

void
STData::setFieldH192(uint192 const& v)
{
    inner_type_ = STI_UINT192;
    data_ = detail::STVar(detail::defaultObject, sfMPTokenIssuanceID);
    setFieldUsingSetValue<STUInt192>(v);
}

void
STData::setFieldH256(uint256 const& v)
{
    inner_type_ = STI_UINT256;
    data_ = detail::STVar(detail::defaultObject, sfLedgerHash);
    setFieldUsingSetValue<STUInt256>(v);
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
STData::setAccountID(AccountID const& v)
{
    inner_type_ = STI_ACCOUNT;
    data_ = detail::STVar(detail::defaultObject, sfAccount);
    setFieldUsingSetValue<STAccount>(v);
}

void
STData::setFieldAmount(STAmount const& v)
{
    inner_type_ = STI_AMOUNT;
    data_ = detail::STVar(detail::defaultObject, sfAmount);
    setFieldUsingAssignment(v);
}

void
STData::setIssue(STIssue const& v)
{
    inner_type_ = STI_ISSUE;
    data_ = detail::STVar(detail::defaultObject, sfAsset);
    setFieldUsingAssignment(v);
}

void
STData::setCurrency(STCurrency const& v)
{
    inner_type_ = STI_CURRENCY;
    data_ = detail::STVar(detail::defaultObject, sfBaseAsset);
    setFieldUsingAssignment(v);
}

void
STData::setFieldNumber(STNumber const& v)
{
    inner_type_ = STI_NUMBER;
    data_ = detail::STVar(detail::defaultObject, sfNumber);
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

uint192
STData::getFieldH192() const
{
    return getFieldByValue<STUInt192>();
}

uint256
STData::getFieldH256() const
{
    return getFieldByValue<STUInt256>();
}

Blob
STData::getFieldVL() const
{
    STBlob empty;
    STBlob const& b = getFieldByConstRef<STBlob>(empty);
    return Blob(b.data(), b.data() + b.size());
}

AccountID
STData::getAccountID() const
{
    return getFieldByValue<STAccount>();
}

STAmount const&
STData::getFieldAmount() const
{
    static STAmount const empty{};
    return getFieldByConstRef<STAmount>(empty);
}

STIssue
STData::getFieldIssue() const
{
    static STIssue const empty{};
    return getFieldByConstRef<STIssue>(empty);
}

STCurrency
STData::getFieldCurrency() const
{
    static STCurrency const empty{};
    return getFieldByConstRef<STCurrency>(empty);
}

STNumber
STData::getFieldNumber() const
{
    static STNumber const empty{};
    return getFieldByConstRef<STNumber>(empty);
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
        try
        {
            if (value.isString())
            {
                STData data(
                    field,
                    beast::lexicalCastThrow<std::uint32_t>(value.asString()));
                return data;
            }
            else if (value.isInt())
            {
                STData data(field, to_unsigned<std::uint32_t>(value.asInt()));
                return data;
            }
            else if (value.isUInt())
            {
                STData data(field, safe_cast<std::uint32_t>(value.asUInt()));
                return data;
            }
            else
            {
                Throw<std::runtime_error>("bad type for UINT32");
            }
        }
        catch (std::exception const&)
        {
            Throw<std::runtime_error>("invalid data for UINT32");
        }
    }
    else if (typeStr == "UINT64")
    {
        try
        {
            if (value.isString())
            {
                auto const str = value.asString();

                std::uint64_t val;

                bool const useBase10 = field.shouldMeta(SField::sMD_BaseTen);

                // if the field is amount, serialize as base 10
                auto [p, ec] = std::from_chars(
                    str.data(),
                    str.data() + str.size(),
                    val,
                    useBase10 ? 10 : 16);

                if (ec != std::errc() || (p != str.data() + str.size()))
                    Throw<std::invalid_argument>("STData: invalid UINT64 data");

                STData data(field, val);
                return data;
            }
            else if (value.isInt())
            {
                STData data(field, to_unsigned<std::uint64_t>(value.asInt()));
                return data;
            }
            else if (value.isUInt())
            {
                STData data(field, safe_cast<std::uint64_t>(value.asUInt()));
                return data;
            }
            else
            {
                Throw<std::runtime_error>("STData: bad type for UINT64");
            }
        }
        catch (std::exception const&)
        {
            Throw<std::runtime_error>("STData: invalid data for UINT64");
        }
    }
    else if (typeStr == "UINT128")
    {
        if (!value.isString())
        {
            Throw<std::runtime_error>("STData: expected string for UINT128");
        }

        uint128 num;

        if (auto const s = value.asString(); !num.parseHex(s))
        {
            if (!s.empty())
            {
                Throw<std::runtime_error>("STData: invalid UINT128 data");
            }

            num.zero();
        }

        STData data(field, num);
        return data;
    }
    else if (typeStr == "UINT192")
    {
        if (!value.isString())
        {
            Throw<std::runtime_error>("STData: expected string for UINT192");
        }

        uint192 num;

        if (auto const s = value.asString(); !num.parseHex(s))
        {
            if (!s.empty())
            {
                Throw<std::runtime_error>("STData: invalid UINT192 data");
            }

            num.zero();
        }

        STData data(field, num);
        return data;
    }
    else if (typeStr == "UINT160")
    {
        if (!value.isString())
        {
            Throw<std::runtime_error>("STData: expected string for UINT160");
        }

        uint160 num;

        if (auto const s = value.asString(); !num.parseHex(s))
        {
            if (!s.empty())
            {
                Throw<std::runtime_error>("STData: invalid UINT160 data");
            }

            num.zero();
        }

        STData data(field, num);
        return data;
    }
    else if (typeStr == "UINT256")
    {
        if (!value.isString())
        {
            Throw<std::runtime_error>("STData: expected string for UINT256");
        }

        uint256 num;

        if (auto const s = value.asString(); !num.parseHex(s))
        {
            if (!s.empty())
            {
                Throw<std::runtime_error>("STData: invalid UINT256 data");
            }

            num.zero();
        }
        STData data(field, num);
        return data;
    }
    else if (typeStr == "VL")
    {
        if (!value.isString())
        {
            Throw<std::runtime_error>("STData: expected string for VL");
        }

        try
        {
            if (auto vBlob = strUnHex(value.asString()))
            {
                STData data(field, *vBlob);
                return data;
            }
            else
            {
                Throw<std::invalid_argument>("invalid data");
            }
        }
        catch (std::exception const&)
        {
            Throw<std::runtime_error>("STData: invalid data");
        }
    }
    else if (typeStr == "AMOUNT")
    {
        try
        {
            STData data(field, amountFromJson(field, value));
            return data;
        }
        catch (std::exception const&)
        {
            Throw<std::runtime_error>("STData: invalid data for AMOUNT");
        }
    }
    else if (typeStr == "ACCOUNT")
    {
        if (!value.isString())
        {
            Throw<std::runtime_error>("STData: expected string for ACCOUNT");
        }

        std::string const strValue = value.asString();

        try
        {
            if (AccountID account; account.parseHex(strValue))
            {
                STData data(field, account);
                return data;
            }

            if (auto result = parseBase58<AccountID>(strValue))
            {
                STData data(field, *result);
                return data;
            }

            Throw<std::runtime_error>("STData: invalid data for ACCOUNT");
        }
        catch (std::exception const&)
        {
            Throw<std::runtime_error>("STData: invalid data for ACCOUNT");
        }
    }
    else if (typeStr == "ISSUE")
    {
        try
        {
            STData data(field, issueFromJson(field, value));
            return data;
        }
        catch (std::exception const&)
        {
            Throw<std::runtime_error>("STData: invalid data for ISSUE");
        }
    }
    else if (typeStr == "CURRENCY")
    {
        try
        {
            STData data(field, currencyFromJson(field, value));
            return data;
        }
        catch (std::exception const&)
        {
            Throw<std::runtime_error>("STData: invalid data for CURRENCY");
        }
    }
    else if (typeStr == "NUMBER")
    {
        if (!value.isString())
        {
            Throw<std::runtime_error>("STData: expected string for NUMBER");
        }

        STNumber number = numberFromJson(field, value);
        STData data(field, number);
        return data;
    }

    // Handle unknown or unsupported type
    Throw<std::runtime_error>("STData: unsupported type string: " + typeStr);
}

}  // namespace ripple
