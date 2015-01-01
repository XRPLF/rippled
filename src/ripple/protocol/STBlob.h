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
class STBlob
    : public STBase
{
public:
    STBlob () = default;

    STBlob (Blob const& v)
        : value (v)
    { }

    STBlob (SField::ref n, Blob const& v)
        : STBase (n), value (v)
    { }

    STBlob (SField::ref n)
        : STBase (n)
    { }

    STBlob (SerializerIterator&, SField::ref name = sfGeneric);

    static
    std::unique_ptr<STBase>
    deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::make_unique<STBlob> (name, sit.getVL ());
    }

    SerializedTypeID
    getSType () const override
    {
        return STI_VL;
    }

    std::string
    getText () const override;

    void
    add (Serializer& s) const override
    {
        assert (fName->isBinary ());
        assert ((fName->fieldType == STI_VL) ||
            (fName->fieldType == STI_ACCOUNT));
        s.addVL (value);
    }

    Blob const&
    peekValue () const
    {
        return value;
    }

    Blob&
    peekValue ()
    {
        return value;
    }

    Blob
    getValue () const
    {
        return value;
    }

    void
    setValue (Blob const& v)
    {
        value = v;
    }

    explicit
    operator Blob () const
    {
        return value;
    }

    bool
    isEquivalent (const STBase& t) const override;

    bool
    isDefault () const override
    {
        return value.empty ();
    }

    std::unique_ptr<STBase>
    duplicate () const override
    {
        return std::make_unique<STBlob>(*this);
    }

private:
    Blob value;
};

} // ripple

#endif
