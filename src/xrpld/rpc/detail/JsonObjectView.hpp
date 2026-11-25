#pragma once

#include <xrpld/rpc/detail/JsonFieldView.hpp>

#include <xrpl/json/json_value.h>

#include <rpcspec/Concepts.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace xrpl::rpc {

/**
 * The request params object the spec DSL resolves fields from.
 *
 * A separate type from JsonFieldView, as in the boost backend, so that a root — which
 * has no key of its own — cannot be passed where a field is expected.
 */
class JsonObjectView
{
    json::Value const* readValue_;
    json::Value* writeValue_;

public:
    explicit JsonObjectView(json::Value& value) noexcept : readValue_(&value), writeValue_(&value)
    {
    }

    explicit JsonObjectView(json::Value const& value) noexcept
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

    [[nodiscard]] JsonFieldView
    child(std::string_view key)
    {
        if (writeValue_ == nullptr)
            return std::as_const(*this).child(key);

        std::string const name{key};
        if (!writeValue_->isObject() || !writeValue_->isMember(name))
            return JsonFieldView::absent(key);
        return {&(*writeValue_)[name], key};
    }

    [[nodiscard]] JsonFieldView
    child(std::string_view key) const
    {
        std::string const name{key};
        if (!readValue_->isObject() || !readValue_->isMember(name))
            return JsonFieldView::absent(key);
        return {&(*readValue_)[name], key};
    }
};

static_assert(::rpc::spec::SomeObjectView<JsonObjectView>);

}  // namespace xrpl::rpc

namespace rpc::spec {

// Binds json::Value to xrpld's view, so a spec can be handed request params directly.
template <>
struct ObjectViewFor<::json::Value>
{
    using Type = ::xrpl::rpc::JsonObjectView;
};

}  // namespace rpc::spec
