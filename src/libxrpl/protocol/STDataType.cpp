#include <xrpl/protocol/STDataType.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/detail/STVar.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl {

// The types an STData holds, by their JSON names.
static constexpr auto kDataTypes = std::to_array<std::pair<std::string_view, SerializedTypeID>>({
    {"UINT8", STI_UINT8},
    {"UINT16", STI_UINT16},
    {"UINT32", STI_UINT32},
    {"UINT64", STI_UINT64},
    {"UINT128", STI_UINT128},
    {"UINT160", STI_UINT160},
    {"UINT192", STI_UINT192},
    {"UINT256", STI_UINT256},
    {"VL", STI_VL},
    {"ACCOUNT", STI_ACCOUNT},
    {"AMOUNT", STI_AMOUNT},
    {"ISSUE", STI_ISSUE},
    {"CURRENCY", STI_CURRENCY},
    {"NUMBER", STI_NUMBER},
});

bool
isDataType(std::uint16_t type)
{
    return std::ranges::any_of(kDataTypes, [type](auto const& entry) {
        return static_cast<std::uint16_t>(entry.second) == type;
    });
}

STDataType::STDataType(SField const& n) : STBase(n), innerType_(STI_NOTPRESENT)
{
}

STDataType::STDataType(SField const& n, SerializedTypeID v)
    : STBase(n), innerType_(v), default_(false)
{
}

STDataType::STDataType(SerialIter& sit, SField const& name)
    : STBase(name), innerType_(sit.get16()), default_(false)
{
    if (!isDataType(innerType_))
        Throw<std::runtime_error>("STDataType: unknown type");
}

STBase*
STDataType::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STDataType::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STDataType::getSType() const
{
    return STI_DATATYPE;
}

void
STDataType::setInnerSType(SerializedTypeID v)
{
    innerType_ = v;
}

void
STDataType::add(Serializer& s) const
{
    s.add16(innerType_);
}

bool
STDataType::isEquivalent(STBase const& t) const
{
    auto const* const tPtr = dynamic_cast<STDataType const*>(&t);
    return (tPtr != nullptr) && (default_ == tPtr->default_) && (innerType_ == tPtr->innerType_);
}

bool
STDataType::isDefault() const
{
    return default_;
}

std::string
STDataType::getInnerTypeString() const
{
    std::string innerTypeStr = "Unknown";
    // Optionally, convert inner_type_ to its string representation if mappings
    // exist
    switch (static_cast<SerializedTypeID>(innerType_))
    {
        case STI_UINT8:
            innerTypeStr = "UINT8";
            break;
        case STI_UINT16:
            innerTypeStr = "UINT16";
            break;
        case STI_UINT32:
            innerTypeStr = "UINT32";
            break;
        case STI_UINT64:
            innerTypeStr = "UINT64";
            break;
        case STI_UINT128:
            innerTypeStr = "UINT128";
            break;
        case STI_UINT160:
            innerTypeStr = "UINT160";
            break;
        case STI_UINT192:
            innerTypeStr = "UINT192";
            break;
        case STI_UINT256:
            innerTypeStr = "UINT256";
            break;
        case STI_VL:
            innerTypeStr = "VL";
            break;
        case STI_ACCOUNT:
            innerTypeStr = "ACCOUNT";
            break;
        case STI_AMOUNT:
            innerTypeStr = "AMOUNT";
            break;
        case STI_ISSUE:
            innerTypeStr = "ISSUE";
            break;
        case STI_CURRENCY:
            innerTypeStr = "CURRENCY";
            break;
        case STI_NUMBER:
            innerTypeStr = "NUMBER";
            break;
        // Add other known types as needed
        default:
            innerTypeStr = std::to_string(innerType_);
    }

    return innerTypeStr;
}

std::string
STDataType::getText() const
{
    std::string const innerTypeStr = getInnerTypeString();
    return "STDataType{InnerType: " + innerTypeStr + "}";
}

json::Value
STDataType::getJson(JsonOptions) const
{
    json::Value ret(json::ValueType::Object);
    ret[jss::type] = getInnerTypeString();
    return ret;
}

STDataType
dataTypeFromJson(SField const& field, json::Value const& v)
{
    if (!v.isObject())
        Throw<std::runtime_error>("STData: expected object");

    auto const typeStr = v[jss::type].asString();
    auto const it =
        std::ranges::find(kDataTypes, typeStr, [](auto const& entry) { return entry.first; });
    if (it == kDataTypes.end())
        Throw<std::runtime_error>("STData: unsupported type string: " + typeStr);
    return STDataType(field, it->second);
}

}  // namespace xrpl
