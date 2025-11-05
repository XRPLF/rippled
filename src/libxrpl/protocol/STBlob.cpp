#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <string>
#include <utility>

namespace ripple {

STBlob::STBlob(SerialIter& st, SField const& name)
    : STBase(name), value_(st.getVLBuffer())
{
}

STBase*
STBlob::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STBlob::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STBlob::getSType() const
{
    return STI_VL;
}

std::string
STBlob::getText() const
{
    return strHex(value_);
}

void
STBlob::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "ripple::STBlob::add : field is binary");
    XRPL_ASSERT(
        (getFName().fieldType == STI_VL) ||
            (getFName().fieldType == STI_ACCOUNT),
        "ripple::STBlob::add : valid field type");
    s.addVL(value_.data(), value_.size());
}

bool
STBlob::isEquivalent(STBase const& t) const
{
    STBlob const* v = dynamic_cast<STBlob const*>(&t);
    return v && (value_ == v->value_);
}

bool
STBlob::isDefault() const
{
    return value_.empty();
}

}  // namespace ripple
