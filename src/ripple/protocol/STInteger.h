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

#ifndef RIPPLE_PROTOCOL_STINTEGER_H_INCLUDED
#define RIPPLE_PROTOCOL_STINTEGER_H_INCLUDED

#include <ripple/protocol/STBase.h>

namespace ripple {

template <typename Integer>
class STInteger : public STBase
{
public:
    using value_type = Integer;

private:
    Integer value_;

public:
    explicit STInteger(Integer v);
    STInteger(SField const& n, Integer v = 0);
    STInteger(SerialIter& sit, SField const& name);

    STBase*
    copy(std::size_t n, void* buf) const override;

    STBase*
    move(std::size_t n, void* buf) override;

    SerializedTypeID
    getSType() const override;

    Json::Value getJson(JsonOptions) const override;

    std::string
    getText() const override;

    void
    add(Serializer& s) const override;

    bool
    isDefault() const override;

    bool
    isEquivalent(const STBase& t) const override;

    STInteger&
    operator=(value_type const& v);

    value_type
    value() const noexcept;

    void
    setValue(Integer v);

    operator Integer() const;
};

using STUInt8 = STInteger<unsigned char>;
using STUInt16 = STInteger<std::uint16_t>;
using STUInt32 = STInteger<std::uint32_t>;
using STUInt64 = STInteger<std::uint64_t>;

template <typename Integer>
inline STInteger<Integer>::STInteger(Integer v) : value_(v)
{
}

template <typename Integer>
inline STInteger<Integer>::STInteger(SField const& n, Integer v)
    : STBase(n), value_(v)
{
}

template <typename Integer>
inline STBase*
STInteger<Integer>::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

template <typename Integer>
inline STBase*
STInteger<Integer>::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

template <typename Integer>
inline void
STInteger<Integer>::add(Serializer& s) const
{
    assert(getFName().isBinary());
    assert(getFName().fieldType == getSType());
    s.addInteger(value_);
}

template <typename Integer>
inline bool
STInteger<Integer>::isDefault() const
{
    return value_ == 0;
}

template <typename Integer>
inline bool
STInteger<Integer>::isEquivalent(const STBase& t) const
{
    const STInteger* v = dynamic_cast<const STInteger*>(&t);
    return v && (value_ == v->value_);
}

template <typename Integer>
inline STInteger<Integer>&
STInteger<Integer>::operator=(value_type const& v)
{
    value_ = v;
    return *this;
}

template <typename Integer>
inline typename STInteger<Integer>::value_type
STInteger<Integer>::value() const noexcept
{
    return value_;
}

template <typename Integer>
inline void
STInteger<Integer>::setValue(Integer v)
{
    value_ = v;
}

template <typename Integer>
inline STInteger<Integer>::operator Integer() const
{
    return value_;
}

}  // namespace ripple

#endif
