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

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

namespace ripple {

STNumber::STNumber(SField const& field, Number const& value)
    : STBase(field), value_(value)
{
}

STNumber::STNumber(SerialIter& sit, SField const& field) : STBase(field)
{
    // We must call these methods in separate statements
    // to guarantee their order of execution.
    auto mantissa = sit.geti64();
    auto exponent = sit.geti32();
    value_ = Number{mantissa, exponent};
}

SerializedTypeID
STNumber::getSType() const
{
    return STI_NUMBER;
}

std::string
STNumber::getText() const
{
    return to_string(value_);
}

void
STNumber::add(Serializer& s) const
{
    XRPL_ASSERT(
        getFName().isBinary(), "ripple::STNumber::add : field is binary");
    XRPL_ASSERT(
        getFName().fieldType == getSType(),
        "ripple::STNumber::add : field type match");
    s.add64(value_.mantissa());
    s.add32(value_.exponent());
}

Number const&
STNumber::value() const
{
    return value_;
}

void
STNumber::setValue(Number const& v)
{
    value_ = v;
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
    XRPL_ASSERT(
        t.getSType() == this->getSType(),
        "ripple::STNumber::isEquivalent : field type match");
    STNumber const& v = dynamic_cast<STNumber const&>(t);
    return value_ == v;
}

bool
STNumber::isDefault() const
{
    return value_ == Number();
}

std::ostream&
operator<<(std::ostream& out, STNumber const& rhs)
{
    return out << rhs.getText();
}

}  // namespace ripple
