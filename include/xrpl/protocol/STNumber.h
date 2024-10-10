//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef XRPL_PROTOCOL_STNUMBER_H_INCLUDED
#define XRPL_PROTOCOL_STNUMBER_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/STBase.h>

namespace ripple {

class STNumber final : public STBase,
                       public CountedObject<STNumber>,
                       public Number
{
public:
    using value_type = Number;

    STNumber(SField const& n, Number const& v = Number());
    STNumber(SerialIter& sit, SField const& name);

    using Number::operator=;

    SerializedTypeID
    getSType() const override;
    std::string
    getText() const override;
    Json::Value getJson(JsonOptions) const override;
    void
    add(Serializer& s) const override;

    Number const&
    value() const;
    void
    setValue(Number const& v);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    bool
    isEquivalent(STBase const& t) const override;
    bool
    isDefault() const override;
};

}  // namespace ripple

#endif
