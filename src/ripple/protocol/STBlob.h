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

#ifndef RIPPLE_PROTOCOL_STBLOB_H_INCLUDED
#define RIPPLE_PROTOCOL_STBLOB_H_INCLUDED

#include <ripple/protocol/STBase.h>
#include <memory>

namespace ripple {

// variable length byte string
class STBlob : public STBase
{
public:
    STBlob (Blob const& v) : value (v)
    {
        ;
    }
    STBlob (SField::ref n, Blob const& v) : STBase (n), value (v)
    {
        ;
    }
    STBlob (SField::ref n) : STBase (n)
    {
        ;
    }
    STBlob (SerializerIterator&, SField::ref name = sfGeneric);
    STBlob ()
    {
        ;
    }
    static std::unique_ptr<STBase> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<STBase> (construct (sit, name));
    }

    virtual SerializedTypeID getSType () const
    {
        return STI_VL;
    }
    virtual std::string getText () const;
    void add (Serializer& s) const
    {
        assert (fName->isBinary ());
        assert ((fName->fieldType == STI_VL) ||
            (fName->fieldType == STI_ACCOUNT));
        s.addVL (value);
    }

    Blob const& peekValue () const
    {
        return value;
    }
    Blob& peekValue ()
    {
        return value;
    }
    Blob getValue () const
    {
        return value;
    }
    void setValue (Blob const& v)
    {
        value = v;
    }

    operator Blob () const
    {
        return value;
    }
    virtual bool isEquivalent (const STBase& t) const;
    virtual bool isDefault () const
    {
        return value.empty ();
    }

private:
    Blob value;

    virtual STBlob* duplicate () const
    {
        return new STBlob (*this);
    }
    static STBlob* construct (SerializerIterator&, SField::ref);
};

} // ripple

#endif
