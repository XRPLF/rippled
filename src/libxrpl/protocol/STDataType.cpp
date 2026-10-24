#include <xrpl/protocol/STDataType.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/detail/STVar.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

// TODO
STDataType::STDataType(SField const& n) : STBase(n), innerType_(STI_NOTPRESENT)
{
}

STDataType::STDataType(SField const& n, SerializedTypeID v)
    : STBase(n), innerType_(v), default_(false)
{
}

STDataType::STDataType(SerialIter& sit, SField const& name)
    : STBase(name), innerType_(STI_DATA), default_(false)
{
    std::uint16_t const stype = SerializedTypeID(sit.get16());
    innerType_ = stype;
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
    SerializedTypeID typeId = STI_NOTPRESENT;
    json::Value type;
    json::Value const value;

    if (!v.isObject())
    {
        Throw<std::runtime_error>("STData: expected object");
    }

    type = v[jss::type];
    auto const typeStr = type.asString();

    if (typeStr == "UINT8")
    {
        typeId = STI_UINT8;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "UINT16")
    {
        typeId = STI_UINT16;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "UINT32")
    {
        typeId = STI_UINT32;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "UINT64")
    {
        typeId = STI_UINT64;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "UINT128")
    {
        typeId = STI_UINT128;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "UINT160")
    {
        typeId = STI_UINT160;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "UINT192")
    {
        typeId = STI_UINT192;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "UINT256")
    {
        typeId = STI_UINT256;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "VL")
    {
        typeId = STI_VL;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "ACCOUNT")
    {
        typeId = STI_ACCOUNT;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "AMOUNT")
    {
        typeId = STI_AMOUNT;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "ISSUE")
    {
        typeId = STI_ISSUE;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "CURRENCY")
    {
        typeId = STI_CURRENCY;
        STDataType data(field, typeId);
        return data;
    }
    if (typeStr == "NUMBER")
    {
        typeId = STI_NUMBER;
        STDataType data(field, typeId);
        return data;
    }

    // Handle unknown or unsupported type
    Throw<std::runtime_error>("STData: unsupported type string: " + typeStr);
}

}  // namespace xrpl
