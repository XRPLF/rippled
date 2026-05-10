#include <xrpl/protocol/STBlob.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/basics/TraceLog.h>

#include <cstddef>
#include <string>
#include <utility>

namespace xrpl {

STBlob::STBlob(SerialIter& st, SField const& name) : STBase(name), value_(st.getVLBuffer())
{
}

STBase*
STBlob::copy(std::size_t n, void* buf) const
{
    TRACE_FUNC();
    return emplace(n, buf, *this);
}

STBase*
STBlob::move(std::size_t n, void* buf)
{
    TRACE_FUNC();
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STBlob::getSType() const
{
    TRACE_FUNC();
    return STI_VL;
}

std::string
STBlob::getText() const
{
    TRACE_FUNC();
    return strHex(value_);
}

void
STBlob::add(Serializer& s) const
{
    TRACE_FUNC();
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STBlob::add : field is binary");
    XRPL_ASSERT(
        (getFName().fieldType == STI_VL) || (getFName().fieldType == STI_ACCOUNT),
        "xrpl::STBlob::add : valid field type");
    s.addVL(value_.data(), value_.size());
}

bool
STBlob::isEquivalent(STBase const& t) const
{
    TRACE_FUNC();
    STBlob const* v = dynamic_cast<STBlob const*>(&t);
    return (v != nullptr) && (value_ == v->value_);
}

bool
STBlob::isDefault() const
{
    TRACE_FUNC();
    return value_.empty();
}

}  // namespace xrpl
