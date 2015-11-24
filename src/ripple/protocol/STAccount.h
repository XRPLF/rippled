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

#ifndef RIPPLE_PROTOCOL_STACCOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_STACCOUNT_H_INCLUDED

#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/STBase.h>
#include <string>

namespace ripple {

class STAccount final
    : public STBase
{
private:
    // The original implementation of STAccount kept the value in an STBlob.
    // But an STAccount is always 160 bits, so we can store it with less
    // overhead in a ripple::uint160.  However, so the serialized format of the
    // STAccount stays unchanged, we serialize and deserialize like an STBlob.
    uint160 value_;
    bool default_;

public:
    using value_type = AccountID;

    STAccount ();
    STAccount (SField const& n);
    STAccount (SField const& n, Buffer&& v);
    STAccount (SerialIter& sit, SField const& name);
    STAccount (SField const& n, AccountID const& v);

    STBase*
    copy (std::size_t n, void* buf) const override
    {
        return emplace (n, buf, *this);
    }

    STBase*
    move (std::size_t n, void* buf) override
    {
        return emplace (n, buf, std::move(*this));
    }

    SerializedTypeID getSType () const override
    {
        return STI_ACCOUNT;
    }

    std::string getText () const override;

    void
    add (Serializer& s) const override
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == STI_ACCOUNT);

        // Preserve the serialization behavior of an STBlob:
        //  o If we are default (all zeros) serialize as an empty blob.
        //  o Otherwise serialize 160 bits.
        int const size = isDefault() ? 0 : uint160::bytes;
        s.addVL (value_.data(), size);
    }

    bool
    isEquivalent (const STBase& t) const override
    {
        auto const* const tPtr = dynamic_cast<STAccount const*>(&t);
        return tPtr && (default_ == tPtr->default_) && (value_ == tPtr->value_);
    }

    bool
    isDefault () const override
    {
        return default_;
    }

    STAccount&
    operator= (AccountID const& value)
    {
        setValue (value);
        return *this;
    }

    AccountID
    value() const noexcept
    {
        AccountID result;
        result.copyFrom (value_);
        return result;
    }

    void setValue (AccountID const& v)
    {
        value_.copyFrom (v);
        default_ = false;
    }
};

} // ripple

#endif
