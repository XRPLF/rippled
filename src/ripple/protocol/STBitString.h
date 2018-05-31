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

#include <ripple/protocol/STBase.h>

namespace ripple {

template <std::size_t Bits>
class STBitString final
    : public STBase
{
public:
    using value_type = base_uint<Bits>;

    STBitString () = default;

    STBitString (SField const& n)
        : STBase (n)
    { }

    STBitString (const value_type& v)
        : value_ (v)
    { }

    STBitString (SField const& n, const value_type& v)
        : STBase (n), value_ (v)
    { }

    STBitString (SField const& n, const char* v)
        : STBase (n)
    {
        value_.SetHex (v);
    }

    STBitString (SField const& n, std::string const& v)
        : STBase (n)
    {
        value_.SetHex (v);
    }

    STBitString (SerialIter& sit, SField const& name)
        : STBitString(name, sit.getBitString<Bits>())
    {
    }

    STBase*
    copy (std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move (std::size_t n, void* buf) noexcept override
    {
        return emplace(n, buf, std::move(*this));
    }

    SerializedTypeID
    getSType () const override;

    std::string
    getText () const override
    {
        return to_string (value_);
    }

    bool
    isEquivalent (const STBase& t) const override
    {
        const STBitString* v = dynamic_cast<const STBitString*> (&t);
        return v && (value_ == v->value_);
    }

    void
    add (Serializer& s) const override
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == getSType());
        s.addBitString<Bits> (value_);
    }

    template <typename Tag>
    void setValue (base_uint<Bits, Tag> const& v)
    {
        value_.copyFrom(v);
    }

    value_type const&
    value() const
    {
        return value_;
    }

    operator value_type () const
    {
        return value_;
    }

    bool
    isDefault () const override
    {
        return value_ == zero;
    }

private:
    value_type value_;
};

using STHash128 = STBitString<128>;
using STHash160 = STBitString<160>;
using STHash256 = STBitString<256>;

template <>
inline
SerializedTypeID
STHash128::getSType () const
{
    return STI_HASH128;
}

template <>
inline
SerializedTypeID
STHash160::getSType () const
{
    return STI_HASH160;
}

template <>
inline
SerializedTypeID
STHash256::getSType () const
{
    return STI_HASH256;
}

} // ripple

#endif
