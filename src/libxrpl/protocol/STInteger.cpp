#include <xrpl/basics/Log.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <charconv>
#include <cstdint>
#include <iterator>
#include <string>
#include <system_error>

namespace ripple {

template <>
STInteger<unsigned char>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get8())
{
}

template <>
SerializedTypeID
STUInt8::getSType() const
{
    return STI_UINT8;
}

template <>
std::string
STUInt8::getText() const
{
    if (getFName() == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo(TER::fromInt(value_), token, human))
            return human;

        // LCOV_EXCL_START
        JLOG(debugLog().error())
            << "Unknown result code in metadata: " << value_;
        // LCOV_EXCL_STOP
    }

    return std::to_string(value_);
}

template <>
Json::Value
STUInt8::getJson(JsonOptions) const
{
    if (getFName() == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo(TER::fromInt(value_), token, human))
            return token;

        // LCOV_EXCL_START
        JLOG(debugLog().error())
            << "Unknown result code in metadata: " << value_;
        // LCOV_EXCL_STOP
    }

    return value_;
}

//------------------------------------------------------------------------------

template <>
STInteger<std::uint16_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get16())
{
}

template <>
SerializedTypeID
STUInt16::getSType() const
{
    return STI_UINT16;
}

template <>
std::string
STUInt16::getText() const
{
    if (getFName() == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance().findByType(
            safe_cast<LedgerEntryType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    if (getFName() == sfTransactionType)
    {
        auto item =
            TxFormats::getInstance().findByType(safe_cast<TxType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    return std::to_string(value_);
}

template <>
Json::Value
STUInt16::getJson(JsonOptions) const
{
    if (getFName() == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance().findByType(
            safe_cast<LedgerEntryType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    if (getFName() == sfTransactionType)
    {
        auto item =
            TxFormats::getInstance().findByType(safe_cast<TxType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    return value_;
}

//------------------------------------------------------------------------------

template <>
STInteger<std::uint32_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get32())
{
}

template <>
SerializedTypeID
STUInt32::getSType() const
{
    return STI_UINT32;
}

template <>
std::string
STUInt32::getText() const
{
    if (getFName() == sfPermissionValue)
    {
        auto const permissionName =
            Permission::getInstance().getPermissionName(value_);
        if (permissionName)
            return *permissionName;
    }
    return std::to_string(value_);
}

template <>
Json::Value
STUInt32::getJson(JsonOptions) const
{
    if (getFName() == sfPermissionValue)
    {
        auto const permissionName =
            Permission::getInstance().getPermissionName(value_);
        if (permissionName)
            return *permissionName;
    }

    return value_;
}

//------------------------------------------------------------------------------

template <>
STInteger<std::uint64_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get64())
{
}

template <>
SerializedTypeID
STUInt64::getSType() const
{
    return STI_UINT64;
}

template <>
std::string
STUInt64::getText() const
{
    return std::to_string(value_);
}

template <>
Json::Value
STUInt64::getJson(JsonOptions) const
{
    auto convertToString = [](uint64_t const value, int const base) {
        XRPL_ASSERT(
            base == 10 || base == 16,
            "ripple::STUInt64::getJson : base 10 or 16");
        std::string str(
            base == 10 ? 20 : 16, 0);  // Allocate space depending on base
        auto ret =
            std::to_chars(str.data(), str.data() + str.size(), value, base);
        XRPL_ASSERT(
            ret.ec == std::errc(),
            "ripple::STUInt64::getJson : to_chars succeeded");
        str.resize(std::distance(str.data(), ret.ptr));
        return str;
    };

    if (auto const& fName = getFName(); fName.shouldMeta(SField::sMD_BaseTen))
    {
        return convertToString(value_, 10);  // Convert to base 10
    }

    return convertToString(value_, 16);  // Convert to base 16
}

//------------------------------------------------------------------------------

template <>
STInteger<std::int32_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get32())
{
}

template <>
SerializedTypeID
STInt32::getSType() const
{
    return STI_INT32;
}

template <>
std::string
STInt32::getText() const
{
    return std::to_string(value_);
}

template <>
Json::Value
STInt32::getJson(JsonOptions) const
{
    return value_;
}

}  // namespace ripple
