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

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/STBase.h>

#include <string>

namespace ripple {

class STAccount final : public STBase, public CountedObject<STAccount>
{
private:
    // The original implementation of STAccount kept the value in an STBlob.
    // But an STAccount is always 160 bits, so we can store it with less
    // overhead in a ripple::uint160.  However, so the serialized format of the
    // STAccount stays unchanged, we serialize and deserialize like an STBlob.
    AccountID value_;
    bool default_;

public:
    using value_type = AccountID;

    STAccount();

    STAccount(SField const& n);
    STAccount(SField const& n, Buffer&& v);
    STAccount(SerialIter& sit, SField const& name);
    STAccount(SField const& n, AccountID const& v);

    SerializedTypeID
    getSType() const override;

    std::string
    getText() const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    STAccount&
    operator=(AccountID const& value);

    AccountID const&
    value() const noexcept;

    void
    setValue(AccountID const& v);

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

inline STAccount&
STAccount::operator=(AccountID const& value)
{
    setValue(value);
    return *this;
}

inline AccountID const&
STAccount::value() const noexcept
{
    return value_;
}

inline void
STAccount::setValue(AccountID const& v)
{
    value_ = v;
    default_ = false;
}

inline bool
operator==(STAccount const& lhs, STAccount const& rhs)
{
    return lhs.value() == rhs.value();
}

inline auto
operator<(STAccount const& lhs, STAccount const& rhs)
{
    return lhs.value() < rhs.value();
}

inline bool
operator==(STAccount const& lhs, AccountID const& rhs)
{
    return lhs.value() == rhs;
}

inline auto
operator<(STAccount const& lhs, AccountID const& rhs)
{
    return lhs.value() < rhs;
}

inline auto
operator<(AccountID const& lhs, STAccount const& rhs)
{
    return lhs < rhs.value();
}

}  // namespace ripple

#endif
