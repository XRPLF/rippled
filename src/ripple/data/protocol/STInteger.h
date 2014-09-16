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

#ifndef RIPPLE_STINTEGER_H
#define RIPPLE_STINTEGER_H

#include <ripple/data/protocol/SerializedType.h>

namespace ripple {

template <typename Integer>
class STInteger : public SerializedType
{
public:
    explicit STInteger (Integer v) : value_ (v)
    {
    }

    STInteger (SField::ref n, Integer v = 0) : SerializedType (n), value_ (v)
    {
    }

    static std::unique_ptr<SerializedType> deserialize (
        SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    SerializedTypeID getSType () const
    {
        return STI_UINT8;
    }

    Json::Value getJson (int) const;
    std::string getText () const;

    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == getSType ());
        s.addInteger (value_);
    }

    Integer getValue () const
    {
        return value_;
    }
    void setValue (Integer v)
    {
        value_ = v;
    }

    operator Integer () const
    {
        return value_;
    }
    virtual bool isDefault () const
    {
        return value_ == 0;
    }

    bool isEquivalent (const SerializedType& t) const
    {
        const STInteger* v = dynamic_cast<const STInteger*> (&t);
        return v && (value_ == v->value_);
    }

private:
    Integer value_;

    STInteger* duplicate () const
    {
        return new STInteger (*this);
    }
    static STInteger* construct (SerializerIterator&, SField::ref f);
};

using STUInt8 = STInteger<unsigned char>;
using STUInt16 = STInteger<std::uint16_t>;
using STUInt32 = STInteger<std::uint32_t>;
using STUInt64 = STInteger<std::uint64_t>;

} // ripple

#endif
