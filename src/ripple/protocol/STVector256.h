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

#ifndef RIPPLE_PROTOCOL_SERIALIZEDTYPES_H_INCLUDED
#define RIPPLE_PROTOCOL_SERIALIZEDTYPES_H_INCLUDED

#include <ripple/protocol/STBitString.h>
#include <ripple/protocol/STInteger.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/RippleAddress.h>

namespace ripple {

class STVector256 : public STBase
{
public:
    STVector256 () = default;
    explicit STVector256 (SField::ref n)
        : STBase (n)
    { }
    explicit STVector256 (std::vector<uint256> const& vector)
        : mValue (vector)
    { }

    SerializedTypeID getSType () const
    {
        return STI_VECTOR256;
    }
    void add (Serializer& s) const;

    static
    std::unique_ptr<STBase>
    deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<STBase> (construct (sit, name));
    }

    const std::vector<uint256>&
    peekValue () const
    {
        return mValue;
    }

    std::vector<uint256>&
    peekValue ()
    {
        return mValue;
    }

    virtual bool isEquivalent (const STBase& t) const;
    virtual bool isDefault () const
    {
        return mValue.empty ();
    }

    std::vector<uint256>::size_type
    size () const
    {
        return mValue.size ();
    }
    bool empty () const
    {
        return mValue.empty ();
    }

    std::vector<uint256>::const_reference
    operator[] (std::vector<uint256>::size_type n) const
    {
        return mValue[n];
    }

    void setValue (const STVector256& v)
    {
        mValue = v.mValue;
    }

    void push_back (uint256 const& v)
    {
        mValue.push_back (v);
    }

    void sort ()
    {
        std::sort (mValue.begin (), mValue.end ());
    }

    Json::Value getJson (int) const;

    std::vector<uint256>::const_iterator
    begin() const
    {
        return mValue.begin ();
    }
    std::vector<uint256>::const_iterator
    end() const
    {
        return mValue.end ();
    }

private:
    std::vector<uint256>    mValue;

    STVector256* duplicate () const
    {
        return new STVector256 (*this);
    }
    static STVector256* construct (SerializerIterator&, SField::ref);
};

} // ripple

#endif
