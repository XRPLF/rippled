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
#include <ripple/protocol/STBlob.h>
#include <string>

namespace ripple {

class STAccount final
    : public STBase
{
private:
    // The original implementation of STAccount kept the value in an STBlob.
    // But, over time, it was determined that an STAccount is always 160
    // bits.  So we can store it with less overhead in a ripple::uint160.
    //
    // However, we need to leave the serialization format of the STAccount
    // unchanged.  So, even though we store the value in an uint160, we
    // serialize and deserialize like an STBlob.
    uint160 value_;

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
        assert ((fName->fieldType == STI_VL) ||
            (fName->fieldType == STI_ACCOUNT));

        s.addVL (value_.data(), (160 / 8));
    }

    bool
    isEquivalent (const STBase& t) const override
    {
        auto const* const tPtr = dynamic_cast<STAccount const*>(&t);
        return (tPtr && (value_ == tPtr->value_));
    }

    bool
    isDefault () const override
    {
        return value_ == beast::zero;
    }

    STAccount&
    operator= (AccountID const& value)
    {
        setValueH160(value);
        return *this;
    }

    AccountID
    value() const noexcept
    {
        AccountID result;
        getValueH160(result);
        return result;
    }

    template <typename Tag>
    void setValueH160 (base_uint<160, Tag> const& v)
    {
        value_.copyFrom (v);
    }

    // VFALCO This is a clumsy interface, it should return
    //        the value. And it should not be possible to
    //        have anything other than a uint160 in here.
    //        The base_uint tag should always be `AccountIDTag`.
    template <typename Tag>
    bool getValueH160 (base_uint<160, Tag>& v) const
    {
        v.copyFrom (value_);
        return true;
    }

    bool isValueH160 () const;
};

} // ripple

#endif
