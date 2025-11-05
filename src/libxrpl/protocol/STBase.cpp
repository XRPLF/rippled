#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

namespace ripple {

STBase::STBase() : fName(&sfGeneric)
{
}

STBase::STBase(SField const& n) : fName(&n)
{
    XRPL_ASSERT(fName, "ripple::STBase::STBase : field is set");
}

STBase&
STBase::operator=(STBase const& t)
{
    if (!fName->isUseful())
        fName = t.fName;
    return *this;
}

bool
STBase::operator==(STBase const& t) const
{
    return (getSType() == t.getSType()) && isEquivalent(t);
}

bool
STBase::operator!=(STBase const& t) const
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
    // LCOV_EXCL_START
    UNREACHABLE("ripple::STBase::add : not implemented");
    // LCOV_EXCL_STOP
}

bool
STBase::isEquivalent(STBase const& t) const
{
    XRPL_ASSERT(
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
    XRPL_ASSERT(fName, "ripple::STBase::setFName : field is set");
}

SField const&
STBase::getFName() const
{
    return *fName;
}

void
STBase::addFieldID(Serializer& s) const
{
    XRPL_ASSERT(
        fName->isBinary(), "ripple::STBase::addFieldID : field is binary");
    s.addFieldID(fName->fieldType, fName->fieldValue);
}

//------------------------------------------------------------------------------

std::ostream&
operator<<(std::ostream& out, STBase const& t)
{
    return out << t.getFullText();
}

}  // namespace ripple
