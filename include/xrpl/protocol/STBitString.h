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

#ifndef RIPPLE_PROTOCOL_STBITSTRING_H_INCLUDED
#define RIPPLE_PROTOCOL_STBITSTRING_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/STBase.h>

namespace ripple {

// The template parameter could be an unsigned type, however there's a bug in
// gdb (last checked in gdb 12.1) that prevents gdb from finding the RTTI
// information of a template parameterized by an unsigned type. This RTTI
// information is needed to write gdb pretty printers.
template <int Bits>
class STBitString final : public STBase, public CountedObject<STBitString<Bits>>
{
    static_assert(Bits > 0, "Number of bits must be positive");

public:
    using value_type = base_uint<Bits>;

private:
    value_type value_;

public:
    STBitString() = default;

    STBitString(SField const& n);
    STBitString(const value_type& v);
    STBitString(SField const& n, const value_type& v);
    STBitString(SerialIter& sit, SField const& name);

    SerializedTypeID
    getSType() const override;

    std::string
    getText() const override;

    bool
    isEquivalent(const STBase& t) const override;

    void
    add(Serializer& s) const override;

    bool
    isDefault() const override;

    template <typename Tag>
    void
    setValue(base_uint<Bits, Tag> const& v);

    value_type const&
    value() const;

    operator value_type() const;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

using STUInt128 = STBitString<128>;
using STUInt160 = STBitString<160>;
using STUInt192 = STBitString<192>;
using STUInt256 = STBitString<256>;

template <int Bits>
inline STBitString<Bits>::STBitString(SField const& n) : STBase(n)
{
}

template <int Bits>
inline STBitString<Bits>::STBitString(const value_type& v) : value_(v)
{
}

template <int Bits>
inline STBitString<Bits>::STBitString(SField const& n, const value_type& v)
    : STBase(n), value_(v)
{
}

template <int Bits>
inline STBitString<Bits>::STBitString(SerialIter& sit, SField const& name)
    : STBitString(name, sit.getBitString<Bits>())
{
}

template <int Bits>
STBase*
STBitString<Bits>::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

template <int Bits>
STBase*
STBitString<Bits>::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

template <>
inline SerializedTypeID
STUInt128::getSType() const
{
    return STI_UINT128;
}

template <>
inline SerializedTypeID
STUInt160::getSType() const
{
    return STI_UINT160;
}

template <>
inline SerializedTypeID
STUInt192::getSType() const
{
    return STI_UINT192;
}

template <>
inline SerializedTypeID
STUInt256::getSType() const
{
    return STI_UINT256;
}

template <int Bits>
std::string
STBitString<Bits>::getText() const
{
    return to_string(value_);
}

template <int Bits>
bool
STBitString<Bits>::isEquivalent(const STBase& t) const
{
    const STBitString* v = dynamic_cast<const STBitString*>(&t);
    return v && (value_ == v->value_);
}

template <int Bits>
void
STBitString<Bits>::add(Serializer& s) const
{
    ASSERT(getFName().isBinary(), "ripple::STBitString::add : field is binary");
    ASSERT(
        getFName().fieldType == getSType(),
        "ripple::STBitString::add : field type match");
    s.addBitString<Bits>(value_);
}

template <int Bits>
template <typename Tag>
void
STBitString<Bits>::setValue(base_uint<Bits, Tag> const& v)
{
    value_ = v;
}

template <int Bits>
typename STBitString<Bits>::value_type const&
STBitString<Bits>::value() const
{
    return value_;
}

template <int Bits>
STBitString<Bits>::operator value_type() const
{
    return value_;
}

template <int Bits>
bool
STBitString<Bits>::isDefault() const
{
    return value_ == beast::zero;
}

}  // namespace ripple

#endif
