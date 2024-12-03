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

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/STBase.h>
#include <boost/checked_delete.hpp>

namespace ripple {

STBase::STBase() : fName(&sfGeneric)
{
}

STBase::STBase(SField const& n) : fName(&n)
{
    ASSERT(fName != nullptr, "ripple::STBase::STBase : field is set");
}

STBase&
STBase::operator=(const STBase& t)
{
    if (!fName->isUseful())
        fName = t.fName;
    return *this;
}

bool
STBase::operator==(const STBase& t) const
{
    return (getSType() == t.getSType()) && isEquivalent(t);
}

bool
STBase::operator!=(const STBase& t) const
{
    return (getSType() != t.getSType()) || !isEquivalent(t);
}

STBase*
STBase::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STBase::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STBase::getSType() const
{
    return STI_NOTPRESENT;
}

std::string
STBase::getFullText() const
{
    std::string ret;

    if (getSType() != STI_NOTPRESENT)
    {
        if (fName->hasName())
        {
            ret = fName->fieldName;
            ret += " = ";
        }

        ret += getText();
    }

    return ret;
}

std::string
STBase::getText() const
{
    return std::string();
}

Json::Value
STBase::getJson(JsonOptions /*options*/) const
{
    return getText();
}

void
STBase::add(Serializer& s) const
{
    // Should never be called
    UNREACHABLE("ripple::STBase::add : not implemented");
}

bool
STBase::isEquivalent(const STBase& t) const
{
    ASSERT(
        getSType() == STI_NOTPRESENT,
        "ripple::STBase::isEquivalent : type not present");
    return t.getSType() == STI_NOTPRESENT;
}

bool
STBase::isDefault() const
{
    return true;
}

void
STBase::setFName(SField const& n)
{
    fName = &n;
    ASSERT(fName != nullptr, "ripple::STBase::setFName : field is set");
}

SField const&
STBase::getFName() const
{
    return *fName;
}

void
STBase::addFieldID(Serializer& s) const
{
    ASSERT(fName->isBinary(), "ripple::STBase::addFieldID : field is binary");
    s.addFieldID(fName->fieldType, fName->fieldValue);
}

//------------------------------------------------------------------------------

std::ostream&
operator<<(std::ostream& out, const STBase& t)
{
    return out << t.getFullText();
}

}  // namespace ripple
