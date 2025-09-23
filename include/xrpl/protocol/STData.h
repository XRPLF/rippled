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

#ifndef RIPPLE_PROTOCOL_STDATA_H_INCLUDED
#define RIPPLE_PROTOCOL_STDATA_H_INCLUDED

#include <xrpl/basics/Buffer.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/detail/STVar.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ripple {

class STData final : public STBase
{
private:
    using data_type = detail::STVar;
    std::uint16_t inner_type_;
    data_type data_;
    bool default_{true};

public:
    using value_type = STData;  // Although not directly holding a single value

    STData(SField const& n);
    STData(SField const& n, unsigned char);
    STData(SField const& n, std::uint16_t);
    STData(SField const& n, std::uint32_t);
    STData(SField const& n, std::uint64_t);
    STData(SField const& n, uint128 const&);
    STData(SField const& n, uint160 const&);
    STData(SField const& n, uint192 const&);
    STData(SField const& n, uint256 const&);
    STData(SField const& n, Blob const&);
    STData(SField const& n, Slice const&);
    STData(SField const& n, AccountID const&);
    STData(SField const& n, STAmount const&);
    STData(SField const& n, STIssue const&);
    STData(SField const& n, STCurrency const&);
    STData(SField const& n, STNumber const&);

    STData(SerialIter& sit, SField const& name);

    std::size_t
    size() const;

    SerializedTypeID
    getSType() const override;

    std::string
    getInnerTypeString() const;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(STBase const& t) const override;

    bool
    isDefault() const override;

    SerializedTypeID
    getInnerSType() const noexcept;

    STBase*
    makeFieldPresent();

    void
    setFieldU8(unsigned char);
    void setFieldU16(std::uint16_t);
    void setFieldU32(std::uint32_t);
    void setFieldU64(std::uint64_t);
    void
    setFieldH128(uint128 const&);
    void
    setFieldH160(uint160 const&);
    void
    setFieldH192(uint192 const&);
    void
    setFieldH256(uint256 const&);
    void
    setFieldVL(Blob const&);
    void
    setFieldVL(Slice const&);
    void
    setAccountID(AccountID const&);
    void
    setFieldAmount(STAmount const&);
    void
    setIssue(STIssue const&);
    void
    setCurrency(STCurrency const&);
    void
    setFieldNumber(STNumber const&);

    unsigned char
    getFieldU8() const;
    std::uint16_t
    getFieldU16() const;
    std::uint32_t
    getFieldU32() const;
    std::uint64_t
    getFieldU64() const;
    uint128
    getFieldH128() const;
    uint160
    getFieldH160() const;
    uint192
    getFieldH192() const;
    uint256
    getFieldH256() const;
    Blob
    getFieldVL() const;
    AccountID
    getAccountID() const;
    STAmount const&
    getFieldAmount() const;
    STIssue
    getFieldIssue() const;
    STCurrency
    getFieldCurrency() const;
    STNumber
    getFieldNumber() const;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;

    // Implementation for getting (most) fields that return by value.
    //
    // The remove_cv and remove_reference are necessitated by the STBitString
    // types.  Their value() returns by const ref.  We return those types
    // by value.
    template <
        typename T,
        typename V = typename std::remove_cv<typename std::remove_reference<
            decltype(std::declval<T>().value())>::type>::type>
    V
    getFieldByValue() const;

    // Implementations for getting (most) fields that return by const reference.
    //
    // If an absent optional field is deserialized we don't have anything
    // obvious to return.  So we insist on having the call provide an
    // 'empty' value we return in that circumstance.
    template <typename T, typename V>
    V const&
    getFieldByConstRef(V const& empty) const;

    // Implementation for setting most fields with a setValue() method.
    template <typename T, typename V>
    void
    setFieldUsingSetValue(V value);

    // Implementation for setting fields using assignment
    template <typename T>
    void
    setFieldUsingAssignment(T const& value);
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------

inline SerializedTypeID
STData::getInnerSType() const noexcept
{
    return static_cast<SerializedTypeID>(inner_type_);
}

template <typename T, typename V>
V
STData::getFieldByValue() const
{
    STBase const* rf = &data_.get();

    // if (!rf)
    //     throwFieldNotFound(getFName());

    SerializedTypeID id = rf->getSType();

    if (id == STI_NOTPRESENT)
        Throw<std::runtime_error>("Field not present");

    T const* cf = dynamic_cast<T const*>(rf);

    if (!cf)
        Throw<std::runtime_error>("Wrong field type");

    return cf->value();
}

// Implementations for getting (most) fields that return by const reference.
//
// If an absent optional field is deserialized we don't have anything
// obvious to return.  So we insist on having the call provide an
// 'empty' value we return in that circumstance.
template <typename T, typename V>
V const&
STData::getFieldByConstRef(V const& empty) const
{
    STBase const* rf = &data_.get();

    // if (!rf)
    //     throwFieldNotFound(field);

    SerializedTypeID id = rf->getSType();

    if (id == STI_NOTPRESENT)
        return empty;  // optional field not present

    T const* cf = dynamic_cast<T const*>(rf);

    if (!cf)
        Throw<std::runtime_error>("Wrong field type");

    return *cf;
}

// Implementation for setting most fields with a setValue() method.
template <typename T, typename V>
void
STData::setFieldUsingSetValue(V value)
{
    static_assert(!std::is_lvalue_reference<V>::value, "");

    STBase* rf = &data_.get();

    // if (!rf)
    //     throwFieldNotFound(field);

    if (rf->getSType() == STI_NOTPRESENT)
        rf = makeFieldPresent();

    T* cf = dynamic_cast<T*>(rf);

    if (!cf)
        Throw<std::runtime_error>("Wrong field type");

    cf->setValue(std::move(value));
}

// Implementation for setting fields using assignment
template <typename T>
void
STData::setFieldUsingAssignment(T const& value)
{
    STBase* rf = &data_.get();

    // if (!rf)
    //     throwFieldNotFound(field);

    // if (rf->getSType() == STI_NOTPRESENT)
    //     rf = makeFieldPresent(field);

    T* cf = dynamic_cast<T*>(rf);

    if (!cf)
        Throw<std::runtime_error>("Wrong field type");

    (*cf) = value;
}

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

STData
dataFromJson(SField const& field, Json::Value const& value);

}  // namespace ripple

#endif
