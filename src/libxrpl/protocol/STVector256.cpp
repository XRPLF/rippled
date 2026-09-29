#include <xrpl/protocol/STVector256.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <stdexcept>
#include <utility>

namespace xrpl {

STVector256::STVector256(SerialIter& sit, SField const& name) : STBase(name)
{
    auto const slice = sit.getVL();

    if (slice.size() % UInt256::size() != 0)
    {
        Throw<std::runtime_error>(
            std::format("Bad serialization for STVector256: {}", slice.size()));
    }

    value_.reserve(slice.size() / UInt256::kBytes);

    SerialIter inner{slice};

    while (!inner.empty())
        value_.push_back(inner.get256());
}

STBase*
STVector256::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STVector256::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STVector256::getSType() const
{
    return STI_VECTOR256;
}

bool
STVector256::isDefault() const
{
    return value_.empty();
}

void
STVector256::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STVector256::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == STI_VECTOR256, "xrpl::STVector256::add : valid field type");

    // Because uint256 has no padding and the container stores the values
    // contiguously, they are already in wire layout, so we can serialize
    // them in one go:
    s.addVL(Slice{value_.data(), value_.size() * UInt256::size()});
}

bool
STVector256::isEquivalent(STBase const& t) const
{
    auto const* v = dynamic_cast<STVector256 const*>(&t);
    return (v != nullptr) && (value_ == v->value_);
}

json::Value
STVector256::getJson(JsonOptions) const
{
    json::Value ret(json::ValueType::Array);

    for (auto const& vEntry : value_)
        ret.append(to_string(vEntry));

    return ret;
}

}  // namespace xrpl
