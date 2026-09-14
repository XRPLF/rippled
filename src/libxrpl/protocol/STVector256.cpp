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
#include <stdexcept>
#include <span>
#include <string>
#include <utility>

namespace xrpl {

STVector256::STVector256(SerialIter& sit, SField const& name) : STBase(name)
{
    auto slice = sit.getSlice(sit.getVLDataLength());

    if (slice.size() % uint256::size() != 0)
    {
        Throw<std::runtime_error>(
            "Bad serialization for STVector256: " + std::to_string(slice.size()));
    }

    value_.reserve(slice.size() / uint256::size());

    while (!slice.empty())
    {
        value_.emplace_back(std::span<std::uint8_t const, uint256::size()>{slice.data(), uint256::size()});
        slice += uint256::size();
    }
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
    s.addVL(value_.begin(), value_.end(), value_.size() * (256 / 8));
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
