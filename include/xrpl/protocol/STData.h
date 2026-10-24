#pragma once

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

namespace xrpl {

class STData final : public STBase
{
private:
    using DataType = detail::STVar;
    std::uint16_t innerType_;
    DataType data_;
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

    [[nodiscard]] std::size_t
    size() const;

    [[nodiscard]] SerializedTypeID
    getSType() const override;

    [[nodiscard]] std::string
    getInnerTypeString() const;

    [[nodiscard]] std::string
    getText() const override;

    [[nodiscard]] json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    [[nodiscard]] bool
    isDefault() const override;

    [[nodiscard]] SerializedTypeID
    getInnerSType() const noexcept;

    STBase*
    makeFieldPresent();

    void
    setFieldU8(unsigned char);
    void
    setFieldU16(std::uint16_t);
    void
    setFieldU32(std::uint32_t);
    void
    setFieldU64(std::uint64_t);
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

    [[nodiscard]] unsigned char
    getFieldU8() const;
    [[nodiscard]] std::uint16_t
    getFieldU16() const;
    [[nodiscard]] std::uint32_t
    getFieldU32() const;
    [[nodiscard]] std::uint64_t
    getFieldU64() const;
    [[nodiscard]] uint128
    getFieldH128() const;
    [[nodiscard]] uint160
    getFieldH160() const;
    [[nodiscard]] uint192
    getFieldH192() const;
    [[nodiscard]] uint256
    getFieldH256() const;
    [[nodiscard]] Blob
    getFieldVL() const;
    [[nodiscard]] AccountID
    getAccountID() const;
    [[nodiscard]] STAmount const&
    getFieldAmount() const;
    [[nodiscard]] STIssue
    getFieldIssue() const;
    [[nodiscard]] STCurrency
    getFieldCurrency() const;
    [[nodiscard]] STNumber
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
    template <typename T, typename V = std::remove_cvref_t<decltype(std::declval<T>().value())>>
    V
    getFieldByValue() const;

    // The held value as a `T`, or nullptr when no value is present.
    template <typename T>
    [[nodiscard]] T const*
    getFieldPtr() const;

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
    return static_cast<SerializedTypeID>(innerType_);
}

template <typename T, typename V>
V
STData::getFieldByValue() const
{
    STBase const* rf = &data_.get();

    // if (!rf)
    //     throwFieldNotFound(getFName());

    SerializedTypeID const id = rf->getSType();

    if (id == STI_NOTPRESENT)
        Throw<std::runtime_error>("Field not present");

    T const* cf = dynamic_cast<T const*>(rf);

    if (!cf)
        Throw<std::runtime_error>("Wrong field type");

    return cf->value();
}

template <typename T>
T const*
STData::getFieldPtr() const
{
    STBase const* rf = &data_.get();

    if (rf->getSType() == STI_NOTPRESENT)
        return nullptr;

    T const* cf = dynamic_cast<T const*>(rf);

    if (!cf)
        Throw<std::runtime_error>("Wrong field type");

    return cf;
}

// Implementation for setting most fields with a setValue() method.
template <typename T, typename V>
void
STData::setFieldUsingSetValue(V value)
{
    static_assert(!std::is_lvalue_reference_v<V>);

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
dataFromJson(SField const& field, json::Value const& value);

}  // namespace xrpl
