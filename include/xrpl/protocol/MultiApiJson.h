#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <array>
#include <concepts>
#include <cstdlib>
#include <functional>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace detail {
template <typename T>
constexpr bool kIsIntegralConstant = false;
template <typename I, auto A>
constexpr bool kIsIntegralConstant<std::integral_constant<I, A>&> = true;
template <typename I, auto A>
constexpr bool kIsIntegralConstant<std::integral_constant<I, A> const&> = true;

template <typename T>
concept some_integral_constant = detail::kIsIntegralConstant<T&>;

// This class is designed to wrap a collection of _almost_ identical json::Value
// objects, indexed by version (i.e. there is some mapping of version to object
// index). It is used e.g. when we need to publish JSON data to users supporting
// different API versions. We allow manipulation and inspection of all objects
// at once with `isMember` and `set`, and also individual inspection and updates
// of an object selected by the user by version, using `visitor_t` nested type.
template <unsigned MinVer, unsigned MaxVer>
struct MultiApiJson
{
    static_assert(MinVer <= MaxVer);

    static constexpr auto
    valid(unsigned int v) noexcept -> bool
    {
        return v >= MinVer && v <= MaxVer;
    }

    static constexpr auto
    index(unsigned int v) noexcept -> std::size_t
    {
        return (v < MinVer) ? 0 : static_cast<std::size_t>(v - MinVer);
    }

    static constexpr std::size_t kSize = MaxVer + 1 - MinVer;
    std::array<json::Value, kSize> val = {};

    explicit MultiApiJson(json::Value const& init = {})
    {
        if (init == json::Value{})
            return;  // All elements are already default-initialized
        for (auto& v : val)
            v = init;
    }

    void
    set(char const* key, auto const& v)
        requires std::constructible_from<json::Value, decltype(v)>
    {
        for (auto& a : this->val)
            a[key] = v;
    }

    enum class IsMemberResult : int { None = 0, Some, All };

    [[nodiscard]] IsMemberResult
    isMember(char const* key) const
    {
        int count = 0;
        for (auto& a : this->val)
        {
            if (a.isMember(key))
                count += 1;
        }

        if (count == 0)
            return IsMemberResult::None;
        return count < kSize ? IsMemberResult::Some : IsMemberResult::All;
    }

    static constexpr struct VisitorT final
    {
        // integral_constant version, extra arguments
        template <typename Json, unsigned int Version, typename... Args, typename Fn>
            requires std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(
            Json& json,
            std::integral_constant<unsigned int, Version> const version,
            Fn fn,
            Args&&... args) const
            -> std::invoke_result_t<
                Fn,
                decltype(json.val[0]),
                std::integral_constant<unsigned int, Version>,
                Args&&...>
        {
            static_assert(valid(Version) && index(Version) >= 0 && index(Version) < kSize);
            return std::invoke(fn, json.val[index(Version)], version, std::forward<Args>(args)...);
        }

        // integral_constant version, Json only
        template <typename Json, unsigned int Version, typename Fn>
            requires std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(Json& json, std::integral_constant<unsigned int, Version> const, Fn fn) const
            -> std::invoke_result_t<Fn, decltype(json.val[0])>
        {
            static_assert(valid(Version) && index(Version) >= 0 && index(Version) < kSize);
            return std::invoke(fn, json.val[index(Version)]);
        }

        // unsigned int version, extra arguments
        template <typename Json, typename Version, typename... Args, typename Fn>
            requires(!some_integral_constant<Version>) && std::convertible_to<Version, unsigned> &&
            std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(Json& json, Version version, Fn fn, Args&&... args) const
            -> std::invoke_result_t<Fn, decltype(json.val[0]), Version, Args&&...>
        {
            XRPL_ASSERT(
                valid(version) && index(version) >= 0 && index(version) < kSize,
                "xrpl::detail::MultiApijson::operator<Args...>() : valid "
                "version");
            return std::invoke(fn, json.val[index(version)], version, std::forward<Args>(args)...);
        }

        // unsigned int version, Json only
        template <typename Json, typename Version, typename Fn>
            requires(!some_integral_constant<Version>) && std::convertible_to<Version, unsigned> &&
            std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(Json& json, Version version, Fn fn) const
            -> std::invoke_result_t<Fn, decltype(json.val[0])>
        {
            XRPL_ASSERT(
                valid(version) && index(version) >= 0 && index(version) < kSize,
                "xrpl::detail::MultiApijson::operator() : valid version");
            return std::invoke(fn, json.val[index(version)]);
        }
    } kVisitor = {};

    auto
    visit()
    {
        return [self = this](auto... args)
            requires requires {
                kVisitor(std::declval<MultiApiJson&>(), std::declval<decltype(args)>()...);
            }
        { return kVisitor(*self, std::forward<decltype(args)>(args)...); };
    }

    [[nodiscard]] auto
    visit() const
    {
        return [self = this](auto... args)
            requires requires {
                kVisitor(std::declval<MultiApiJson const&>(), std::declval<decltype(args)>()...);
            }
        { return kVisitor(*self, std::forward<decltype(args)>(args)...); };
    }

    template <typename... Args>
    auto
    visit(Args... args) -> std::invoke_result_t<VisitorT, MultiApiJson&, Args...>
        requires(sizeof...(args) > 0) &&
        requires { kVisitor(*this, std::forward<decltype(args)>(args)...); }
    {
        return kVisitor(*this, std::forward<decltype(args)>(args)...);
    }

    template <typename... Args>
    [[nodiscard]] auto
    visit(Args... args) const -> std::invoke_result_t<VisitorT, MultiApiJson const&, Args...>
        requires(sizeof...(args) > 0) &&
        requires { kVisitor(*this, std::forward<decltype(args)>(args)...); }
    {
        return kVisitor(*this, std::forward<decltype(args)>(args)...);
    }
};

}  // namespace detail

// Wrapper for Json for all supported API versions.
using MultiApiJson =
    detail::MultiApiJson<RPC::kApiMinimumSupportedVersion, RPC::kApiMaximumValidVersion>;

}  // namespace xrpl
