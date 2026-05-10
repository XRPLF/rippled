#include <xrpl/protocol/STBase.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/basics/TraceLog.h>

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

namespace xrpl {

STBase::STBase() : fName_(&kSF_GENERIC)
{
}

STBase::STBase(SField const& n) : fName_(&n)
{
    TRACE_FUNC();
    XRPL_ASSERT(fName_, "xrpl::STBase::STBase : field is set");
}

STBase&
STBase::operator=(STBase const& t)
{
    TRACE_FUNC();
    if (this == &t)
        return *this;

    if (!fName_->isUseful())
        fName_ = t.fName_;
    return *this;
}

bool
STBase::operator==(STBase const& t) const
{
    TRACE_FUNC();
    return (getSType() == t.getSType()) && isEquivalent(t);
}

bool
STBase::operator!=(STBase const& t) const
{
    TRACE_FUNC();
    return (getSType() != t.getSType()) || !isEquivalent(t);
}

STBase*
STBase::copy(std::size_t n, void* buf) const
{
    TRACE_FUNC();
    return emplace(n, buf, *this);
}

STBase*
STBase::move(std::size_t n, void* buf)
{
    TRACE_FUNC();
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STBase::getSType() const
{
    TRACE_FUNC();
    return STI_NOTPRESENT;
}

std::string
STBase::getFullText() const
{
    TRACE_FUNC();
    std::string ret;

    if (getSType() != STI_NOTPRESENT)
    {
        if (fName_->hasName())
        {
            ret = fName_->fieldName;
            ret += " = ";
        }

        ret += getText();
    }

    return ret;
}

std::string
STBase::getText() const
{
    TRACE_FUNC();
    return std::string();
}

json::Value
STBase::getJson(JsonOptions /*options*/) const
{
    TRACE_FUNC();
    return getText();
}

void
STBase::add(Serializer& s) const
{
    TRACE_FUNC();
    // Should never be called
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::STBase::add : not implemented");
    // LCOV_EXCL_STOP
}

bool
STBase::isEquivalent(STBase const& t) const
{
    TRACE_FUNC();
    XRPL_ASSERT(getSType() == STI_NOTPRESENT, "xrpl::STBase::isEquivalent : type not present");
    return t.getSType() == STI_NOTPRESENT;
}

bool
STBase::isDefault() const
{
    TRACE_FUNC();
    return true;
}

void
STBase::setFName(SField const& n)
{
    TRACE_FUNC();
    fName_ = &n;
    XRPL_ASSERT(fName_, "xrpl::STBase::setFName : field is set");
}

SField const&
STBase::getFName() const
{
    TRACE_FUNC();
    return *fName_;
}

void
STBase::addFieldID(Serializer& s) const
{
    TRACE_FUNC();
    XRPL_ASSERT(fName_->isBinary(), "xrpl::STBase::addFieldID : field is binary");
    s.addFieldID(fName_->fieldType, fName_->fieldValue);
}

//------------------------------------------------------------------------------

std::ostream&
operator<<(std::ostream& out, STBase const& t)
{
    TRACE_FUNC();
    return out << t.getFullText();
}

}  // namespace xrpl
