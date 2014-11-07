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

#include <ripple/protocol/SerializedType.h>

namespace ripple {

template <std::size_t Bits>
class STBitString : public SerializedType
{
public:
    typedef base_uint<Bits> BitString;

    STBitString ()                                    {}
    STBitString (SField::ref n) : SerializedType (n)  {}
    STBitString (const BitString& v) : bitString_ (v) {}

    STBitString (SField::ref n, const BitString& v)
            : SerializedType (n), bitString_ (v)
    {
    }

    STBitString (SField::ref n, const char* v) : SerializedType (n)
    {
        bitString_.SetHex (v);
    }

    STBitString (SField::ref n, std::string const& v) : SerializedType (n)
    {
        bitString_.SetHex (v);
    }

    static std::unique_ptr<SerializedType> deserialize (
        SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const;

    std::string getText () const
    {
        return to_string (bitString_);
    }

    bool isEquivalent (const SerializedType& t) const
    {
        const STBitString* v = dynamic_cast<const STBitString*> (&t);
        return v && (bitString_ == v->bitString_);
    }

    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == getSType());
        s.addBitString<Bits> (bitString_);
    }

    const BitString& getValue () const
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

    virtual bool isDefault () const
    {
        return bitString_ == zero;
    }

private:
    BitString bitString_;

    STBitString* duplicate () const
    {
        return new STBitString (*this);
    }

    static STBitString* construct (SerializerIterator& u, SField::ref name)
    {
        return new STBitString (name, u.getBitString<Bits> ());
    }
};

template <>
inline SerializedTypeID STBitString<128>::getSType () const
{
    return STI_HASH128;
}

template <>
inline SerializedTypeID STBitString<160>::getSType () const
{
    return STI_HASH160;
}

template <>
inline SerializedTypeID STBitString<256>::getSType () const
{
    return STI_HASH256;
}

using STHash128 = STBitString<128>;
using STHash160 = STBitString<160>;
using STHash256 = STBitString<256>;

} // ripple

#endif
