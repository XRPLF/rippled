//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_STCURRENCY_H_INCLUDED
#define RIPPLE_PROTOCOL_STCURRENCY_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/UintTypes.h>

namespace ripple {

class STCurrency final : public STBase
{
private:
    Currency currency_{};

public:
    using value_type = Currency;

    STCurrency() = default;

    explicit STCurrency(SerialIter& sit, SField const& name);

    explicit STCurrency(SField const& name, Currency const& currency);

    explicit STCurrency(SField const& name);

    Currency const&
    currency() const;

    Currency const&
    value() const noexcept;

    void
    setCurrency(Currency const& currency);

    SerializedTypeID
    getSType() const override;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

private:
    static std::unique_ptr<STCurrency>
    construct(SerialIter&, SField const& name);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

STCurrency
currencyFromJson(SField const& name, Json::Value const& v);

inline Currency const&
STCurrency::currency() const
{
    return currency_;
}

inline Currency const&
STCurrency::value() const noexcept
{
    return currency_;
}

inline void
STCurrency::setCurrency(Currency const& currency)
{
    currency_ = currency;
}

inline bool
operator==(STCurrency const& lhs, STCurrency const& rhs)
{
    return lhs.currency() == rhs.currency();
}

inline bool
operator!=(STCurrency const& lhs, STCurrency const& rhs)
{
    return !operator==(lhs, rhs);
}

inline bool
operator<(STCurrency const& lhs, STCurrency const& rhs)
{
    return lhs.currency() < rhs.currency();
}

inline bool
operator==(STCurrency const& lhs, Currency const& rhs)
{
    return lhs.currency() == rhs;
}

inline bool
operator<(STCurrency const& lhs, Currency const& rhs)
{
    return lhs.currency() < rhs;
}

}  // namespace ripple

#endif
