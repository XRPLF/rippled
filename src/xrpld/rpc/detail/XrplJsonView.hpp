#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>

#include <rpcspec/Concepts.hpp>
#include <rpcspec/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>  // IWYU pragma: keep
#include <string>
#include <string_view>
#include <type_traits>

namespace xrpl::rpc {

/**
 * The json::Value backend for the spec DSL: a view of one resolved field.
 *
 * The DSL is written against the SomeFieldView / SomeObjectView concepts rather than any
 * JSON type.
 *
 * A null pointer means the field is absent, which every predicate has to answer for. Which
 * of the two pointers is set carries const-correctness: a view built from a const value
 * cannot be written through, so a modifier cannot run during the check phase.
 */
class XrplJsonFieldView
{
    json::Value const* readValue_;
    json::Value* writeValue_;

    std::string_view key_;

public:
    XrplJsonFieldView(json::Value* value, std::string_view key) noexcept
        : readValue_(value), writeValue_(value), key_(key)
    {
    }

    XrplJsonFieldView(json::Value const* value, std::string_view key) noexcept
        : readValue_(value), writeValue_(nullptr), key_(key)
    {
    }

    [[nodiscard]] static XrplJsonFieldView
    absentMutable(std::string_view key) noexcept
    {
        return {static_cast<json::Value*>(nullptr), key};
    }

    [[nodiscard]] static XrplJsonFieldView
    absentConst(std::string_view key) noexcept
    {
        return {static_cast<json::Value const*>(nullptr), key};
    }

    [[nodiscard]] std::string_view
    key() const noexcept
    {
        return key_;
    }

    [[nodiscard]] bool
    present() const noexcept
    {
        return readValue_ != nullptr;
    }

    // Not json::Value::isIntegral(), which is also true for a boolean: boost's is_int64()
    // is not, and a spec must not accept `true` as a number on one server only.
    [[nodiscard]] bool
    isInt64() const noexcept
    {
        return readValue_ != nullptr && (readValue_->isInt() || readValue_->isUInt());
    }

    [[nodiscard]] std::int64_t
    asInt64() const
    {
        return readValue_->isInt() ? static_cast<std::int64_t>(readValue_->asInt())
                                   : static_cast<std::int64_t>(readValue_->asUInt());
    }

    // A question about the value, not the storage: a non-negative Int is a uint32.
    // Both of json::Value's integer types are 32 bits, so that is the only check.
    [[nodiscard]] bool
    isUint32() const noexcept
    {
        if (readValue_ == nullptr)
            return false;
        if (readValue_->isUInt())
            return true;
        return readValue_->isInt() && readValue_->asInt() >= 0;
    }

    [[nodiscard]] std::uint32_t
    asUint32() const
    {
        return readValue_->isUInt() ? readValue_->asUInt()
                                    : static_cast<std::uint32_t>(readValue_->asInt());
    }

    [[nodiscard]] bool
    isBool() const noexcept
    {
        return readValue_ != nullptr && readValue_->isBool();
    }

    [[nodiscard]] bool
    asBool() const
    {
        return readValue_->asBool();
    }

    [[nodiscard]] bool
    isString() const noexcept
    {
        return readValue_ != nullptr && readValue_->isString();
    }

    [[nodiscard]] std::string_view
    asString() const
    {
        auto const* str = readValue_->asCString();
        return str != nullptr ? std::string_view{str} : std::string_view{};
    }

    [[nodiscard]] bool
    isDouble() const noexcept
    {
        return readValue_ != nullptr && readValue_->isDouble();
    }

    [[nodiscard]] double
    asDouble() const
    {
        return readValue_->asDouble();
    }

    [[nodiscard]] bool
    isObject() const noexcept
    {
        return readValue_ != nullptr && readValue_->isObject();
    }

    [[nodiscard]] bool
    isArray() const noexcept
    {
        return readValue_ != nullptr && readValue_->isArray();
    }

    [[nodiscard]] std::size_t
    arraySize() const noexcept
    {
        if (readValue_ == nullptr || !readValue_->isArray())
            return 0;
        return readValue_->size();
    }

    [[nodiscard]] std::size_t
    objectSize() const noexcept
    {
        if (readValue_ == nullptr || !readValue_->isObject())
            return 0;
        return readValue_->size();
    }

    [[nodiscard]] XrplJsonFieldView
    child(std::string_view childKey) const
    {
        // Guarded rather than indexed: the non-const operator[] inserts a null member.
        std::string const key{childKey};

        if (writeValue_ != nullptr)
        {
            if (!writeValue_->isObject() || !writeValue_->isMember(key))
                return absentMutable(childKey);
            return {&(*writeValue_)[key], childKey};
        }

        if (readValue_ == nullptr || !readValue_->isObject() || !readValue_->isMember(key))
            return absentConst(childKey);
        return {&(*readValue_)[key], childKey};
    }

    // Inherits this field's key, so an error about an element names the array.
    [[nodiscard]] XrplJsonFieldView
    element(std::size_t idx) const
    {
        if (writeValue_ != nullptr)
        {
            if (!writeValue_->isArray() || idx >= writeValue_->size())
                return absentMutable(key_);
            return {&(*writeValue_)[static_cast<json::UInt>(idx)], key_};
        }

        if (readValue_ == nullptr || !readValue_->isArray() || idx >= readValue_->size())
            return absentConst(key_);
        return {&(*readValue_)[static_cast<json::UInt>(idx)], key_};
    }

    template <typename T>
    [[nodiscard]] bool
    is() const noexcept
    {
        if constexpr (std::is_same_v<T, std::int64_t>)
        {
            return isInt64();
        }
        else if constexpr (std::is_same_v<T, std::uint32_t>)
        {
            return isUint32();
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return isBool();
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return isString();
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            return isDouble();
        }
        else if constexpr (std::is_same_v<T, ::rpc::spec::JsonObject>)
        {
            return isObject();
        }
        else if constexpr (std::is_same_v<T, ::rpc::spec::JsonArray>)
        {
            return isArray();
        }
        else
        {
            static_assert(false, "xrpl::rpc::XrplJsonFieldView::is : unsupported type");
        }
    }

    // Only reachable from a modifier, which only ever runs against a mutable view.
    //
    // json::Value has no 64-bit integer type, so the value goes into whichever of its two
    // 32-bit types can hold it. Casting everything to json::Int would wrap anything above
    // INT32_MAX, and the spec's toNumber modifier deliberately produces values up to
    // UINT32_MAX (ledger_entry's oracle_document_id), which the boost backend stores fine.
    void
    set(std::int64_t value)
    {
        if (value >= 0)
        {
            XRPL_ASSERT(
                value <= std::numeric_limits<json::UInt>::max(),
                "xrpl::rpc::XrplJsonFieldView::set : value representable as json::UInt");
            *writeValue_ = static_cast<json::UInt>(value);
        }
        else
        {
            XRPL_ASSERT(
                value >= std::numeric_limits<json::Int>::min(),
                "xrpl::rpc::XrplJsonFieldView::set : value representable as json::Int");
            *writeValue_ = static_cast<json::Int>(value);
        }
    }

    void
    set(std::uint32_t value)
    {
        *writeValue_ = static_cast<json::UInt>(value);
    }

    void
    set(std::string_view value)
    {
        *writeValue_ = std::string{value};
    }

    void
    set(bool value)
    {
        *writeValue_ = value;
    }

    void
    set(double value)
    {
        *writeValue_ = value;
    }
};

static_assert(::rpc::spec::SomeFieldView<XrplJsonFieldView>);

/**
 * The request params object the spec DSL resolves fields from.
 *
 * A separate type from XrplJsonFieldView, as in the boost backend, so that a root — which
 * has no key of its own — cannot be passed where a field is expected.
 */
class XrplJsonObjectView
{
    json::Value const* readValue_;
    json::Value* writeValue_;

public:
    explicit XrplJsonObjectView(json::Value& value) noexcept
        : readValue_(&value), writeValue_(&value)
    {
    }

    explicit XrplJsonObjectView(json::Value const& value) noexcept
        : readValue_(&value), writeValue_(nullptr)
    {
    }

    [[nodiscard]] bool
    isObject() const noexcept
    {
        return readValue_->isObject();
    }

    [[nodiscard]] bool
    isArray() const noexcept
    {
        return readValue_->isArray();
    }

    [[nodiscard]] XrplJsonFieldView
    child(std::string_view key)
    {
        std::string const name{key};
        if (writeValue_ == nullptr || !writeValue_->isObject() || !writeValue_->isMember(name))
            return XrplJsonFieldView::absentMutable(key);
        return {&(*writeValue_)[name], key};
    }

    [[nodiscard]] XrplJsonFieldView
    child(std::string_view key) const
    {
        std::string const name{key};
        if (!readValue_->isObject() || !readValue_->isMember(name))
            return XrplJsonFieldView::absentConst(key);
        return {&(*readValue_)[name], key};
    }
};

static_assert(::rpc::spec::SomeObjectView<XrplJsonObjectView>);

}  // namespace xrpl::rpc

namespace rpc::spec {

// Binds json::Value to xrpld's view, so a spec can be handed request params directly.
template <>
struct ObjectViewFor<::json::Value>
{
    using Type = ::xrpl::rpc::XrplJsonObjectView;
};

}  // namespace rpc::spec
