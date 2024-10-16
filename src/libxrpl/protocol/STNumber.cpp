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

#include <xrpl/protocol/STNumber.h>

#include <xrpl/protocol/SField.h>

namespace ripple {

STNumber::STNumber(SField const& field, Number const& value)
    : STBase(field), Number(value)
{
}

STNumber::STNumber(SerialIter& sit, SField const& field) : STBase(field)
{
    // We must call these methods in separate statements
    // to guarantee their order of execution.
    auto mantissa = sit.geti64();
    auto exponent = sit.geti32();
    *this = Number{mantissa, exponent};
}

SerializedTypeID
STNumber::getSType() const
{
    return STI_NUMBER;
}

std::string
STNumber::getText() const
{
    return to_string(*this);
}

Json::Value
STNumber::getJson(JsonOptions) const
{
    return to_string(*this);
}

void
STNumber::add(Serializer& s) const
{
    assert(getFName().isBinary());
    assert(getFName().fieldType == getSType());
    s.add64(this->mantissa());
    s.add32(this->exponent());
}

Number const&
STNumber::value() const
{
    return *this;
}

void
STNumber::setValue(Number const& v)
{
    *this = v;
}

STBase*
STNumber::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STNumber::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

bool
STNumber::isEquivalent(STBase const& t) const
{
    assert(t.getSType() == this->getSType());
    Number const& v = dynamic_cast<Number const&>(t);
    return *this == v;
}

bool
STNumber::isDefault() const
{
    return *this == Number();
}

std::ostream&
operator<<(std::ostream& out, STNumber const& rhs)
{
    return out << rhs.getText();
}

}  // namespace ripple
