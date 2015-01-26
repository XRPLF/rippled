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

#ifndef RIPPLE_PROTOCOL_STBITS_H_INCLUDED
#define RIPPLE_PROTOCOL_STBITS_H_INCLUDED

#include <ripple/protocol/STBase.h>

namespace ripple {

template <std::size_t Bits>
class STBitString final
    : public STBase
{
public:
    typedef base_uint<Bits> BitString;

    STBitString () = default;

    STBitString (SField::ref n)
        : STBase (n)
    { }

    STBitString (const BitString& v)
        : bitString_ (v)
    { }

    STBitString (SField::ref n, const BitString& v)
        : STBase (n), bitString_ (v)
    { }

    STBitString (SField::ref n, const char* v)
        : STBase (n)
    {
        bitString_.SetHex (v);
    }

    STBitString (SField::ref n, std::string const& v)
        : STBase (n)
    {
        bitString_.SetHex (v);
    }

    static
    std::unique_ptr<STBase>
    deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::make_unique<STBitString> (name, sit.getBitString<Bits> ());
    }

    SerializedTypeID
    getSType () const override;

    std::string
    getText () const override
    {
        return to_string (bitString_);
    }

    bool
    isEquivalent (const STBase& t) const override
    {
        const STBitString* v = dynamic_cast<const STBitString*> (&t);
        return v && (bitString_ == v->bitString_);
    }

    void
    add (Serializer& s) const override
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == getSType());
        s.addBitString<Bits> (bitString_);
    }

    const BitString&
    getValue () const
    {
        return bitString_;
    }

    template <typename Tag>
    void setValue (base_uint<Bits, Tag> const& v)
    {
        bitString_.copyFrom(v);
    }

    operator BitString () const
    {
        return bitString_;
    }

    bool
    isDefault () const override
    {
        return bitString_ == zero;
    }

    std::unique_ptr<STBase>
    duplicate () const override
    {
        return std::make_unique<STBitString>(*this);
    }

private:
    BitString bitString_;
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
