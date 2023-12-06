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

#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/STPluginType.h>
#include <iostream>

namespace ripple {

STPluginType::STPluginType(SerialIter& st, SField const& name) : STBase(name)
{
    int type = name.fieldType;
    if (auto const it = SField::pluginSTypes.find(type);
        it != SField::pluginSTypes.end())
    {
        value_ = it->second.fromSerialIter(type, st);
    }
    else
    {
        throw std::runtime_error(
            "Type " + std::to_string(type) + " does not exist");
    }
}

STBase*
STPluginType::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STPluginType::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

int
STPluginType::getSType() const
{
    return getFName().fieldType;
}

std::string
STPluginType::getText() const
{
    int type = getSType();
    if (auto const it = SField::pluginSTypes.find(type);
        it != SField::pluginSTypes.end())
    {
        return it->second.toString(type, value_);
    }
    throw std::runtime_error(
        "Type " + std::to_string(type) + " does not exist");
}

Json::Value STPluginType::getJson(JsonOptions /*options*/) const
{
    int type = getSType();
    if (auto const it = SField::pluginSTypes.find(type);
        it != SField::pluginSTypes.end())
    {
        if (it->second.toJson != NULL)
        {
            return it->second.toJson(type, value_);
        }
        return it->second.toString(type, value_);
    }
    throw std::runtime_error(
        "Type " + std::to_string(type) + " does not exist");
}

void
STPluginType::add(Serializer& s) const
{
    int type = getSType();
    if (auto const it = SField::pluginSTypes.find(type);
        it != SField::pluginSTypes.end())
    {
        return it->second.toSerializer(type, value_, s);
    }
    throw std::runtime_error(
        "Type " + std::to_string(type) + " does not exist");
}

bool
STPluginType::isEquivalent(const STBase& t) const
{
    const STPluginType* v = dynamic_cast<const STPluginType*>(&t);
    return v && (value_ == v->value_);
}

bool
STPluginType::isDefault() const
{
    return value_.empty();
}

}  // namespace ripple
