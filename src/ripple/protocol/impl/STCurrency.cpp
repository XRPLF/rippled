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

#include <ripple/protocol/STCurrency.h>
#include <ripple/protocol/jss.h>

#include <ripple/basics/contract.h>

namespace ripple {

STCurrency::STCurrency(SField const& name) : STBase{name}
{
}

STCurrency::STCurrency(SerialIter& sit, SField const& name) : STBase{name}
{
    currency_ = sit.get160();
}

STCurrency::STCurrency(SField const& name, Currency const& currency)
    : STBase{name}, currency_{currency}
{
}

SerializedTypeID
STCurrency::getSType() const
{
    return STI_CURRENCY;
}

std::string
STCurrency::getText() const
{
    return to_string(currency_);
}

Json::Value STCurrency::getJson(JsonOptions) const
{
    return to_string(currency_);
}

void
STCurrency::add(Serializer& s) const
{
    s.addBitString(currency_);
}

bool
STCurrency::isEquivalent(const STBase& t) const
{
    const STCurrency* v = dynamic_cast<const STCurrency*>(&t);
    return v && (*v == *this);
}

bool
STCurrency::isDefault() const
{
    return isXRP(currency_);
}

std::unique_ptr<STCurrency>
STCurrency::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STCurrency>(sit, name);
}

STBase*
STCurrency::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STCurrency::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

STCurrency
currencyFromJson(SField const& name, Json::Value const& v)
{
    if (!v.isString())
    {
        Throw<std::runtime_error>(
            "currencyFromJson currency must be a string Json value");
    }

    auto const currency = to_currency(v.asString());
    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<std::runtime_error>(
            "currencyFromJson currency must be a valid currency");
    }

    return STCurrency{name, currency};
}

}  // namespace ripple
