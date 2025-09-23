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

#ifndef RIPPLE_PROTOCOL_STDATATYPE_H_INCLUDED
#define RIPPLE_PROTOCOL_STDATATYPE_H_INCLUDED

#include <xrpl/basics/Buffer.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/detail/STVar.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ripple {

class STDataType final : public STBase
{
private:
    std::uint16_t inner_type_;
    bool default_{true};

public:
    using value_type =
        STDataType;  // Although not directly holding a single value

    STDataType(SField const& n);
    STDataType(SField const& n, SerializedTypeID);

    STDataType(SerialIter& sit, SField const& name);

    SerializedTypeID
    getSType() const override;

    std::string
    getInnerTypeString() const;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(STBase const& t) const override;

    bool
    isDefault() const override;

    void setInnerSType(SerializedTypeID);

    SerializedTypeID
    getInnerSType() const noexcept;

    STBase*
    makeFieldPresent();

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------

inline SerializedTypeID
STDataType::getInnerSType() const noexcept
{
    return static_cast<SerializedTypeID>(inner_type_);
}

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

STDataType
dataTypeFromJson(SField const& field, Json::Value const& value);

}  // namespace ripple

#endif
