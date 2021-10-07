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

#include <ripple/beast/utility/Zero.h>
#include <ripple/protocol/STBase.h>

namespace ripple {

template <std::size_t Bits>
class STBitString final : public STBase
{
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

    STBase*
    copy(std::size_t n, void* buf) const override;

    STBase*
    move(std::size_t n, void* buf) override;

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
};

using STHash128 = STBitString<128>;
using STHash160 = STBitString<160>;
using STHash256 = STBitString<256>;

template <std::size_t Bits>
inline STBitString<Bits>::STBitString(SField const& n) : STBase(n)
{
}

template <std::size_t Bits>
inline STBitString<Bits>::STBitString(const value_type& v) : value_(v)
{
}

template <std::size_t Bits>
inline STBitString<Bits>::STBitString(SField const& n, const value_type& v)
    : STBase(n), value_(v)
{
}

template <std::size_t Bits>
inline STBitString<Bits>::STBitString(SerialIter& sit, SField const& name)
    : STBitString(name, sit.getBitString<Bits>())
{
}

template <std::size_t Bits>
STBase*
STBitString<Bits>::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

template <std::size_t Bits>
STBase*
STBitString<Bits>::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

template <>
inline SerializedTypeID
STHash128::getSType() const
{
    return STI_HASH128;
}

template <>
inline SerializedTypeID
STHash160::getSType() const
{
    return STI_HASH160;
}

template <>
inline SerializedTypeID
STHash256::getSType() const
{
    return STI_HASH256;
}

template <std::size_t Bits>
std::string
STBitString<Bits>::getText() const
{
    return to_string(value_);
}

template <std::size_t Bits>
bool
STBitString<Bits>::isEquivalent(const STBase& t) const
{
    const STBitString* v = dynamic_cast<const STBitString*>(&t);
    return v && (value_ == v->value_);
}

template <std::size_t Bits>
void
STBitString<Bits>::add(Serializer& s) const
{
    assert(getFName().isBinary());
    assert(getFName().fieldType == getSType());
    s.addBitString<Bits>(value_);
}

template <std::size_t Bits>
template <typename Tag>
void
STBitString<Bits>::setValue(base_uint<Bits, Tag> const& v)
{
    value_ = v;
}

template <std::size_t Bits>
typename STBitString<Bits>::value_type const&
STBitString<Bits>::value() const
{
    return value_;
}

template <std::size_t Bits>
STBitString<Bits>::operator value_type() const
{
    return value_;
}

template <std::size_t Bits>
bool
STBitString<Bits>::isDefault() const
{
    return value_ == beast::zero;
}

}  // namespace ripple

#endif
