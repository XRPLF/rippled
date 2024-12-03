//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_JSON_MULTIAPIJSON_H_INCLUDED
#define RIPPLE_JSON_MULTIAPIJSON_H_INCLUDED

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <array>
#include <concepts>
#include <cstdlib>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

namespace ripple {

namespace detail {
template <typename T>
constexpr bool is_integral_constant = false;
template <typename I, auto A>
constexpr bool is_integral_constant<std::integral_constant<I, A>&> = true;
template <typename I, auto A>
constexpr bool is_integral_constant<std::integral_constant<I, A> const&> = true;

template <typename T>
concept some_integral_constant = detail::is_integral_constant<T&>;

// This class is designed to wrap a collection of _almost_ identical Json::Value
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

    constexpr static std::size_t size = MaxVer + 1 - MinVer;
    std::array<Json::Value, size> val = {};

    explicit MultiApiJson(Json::Value const& init = {})
    {
        if (init == Json::Value{})
            return;  // All elements are already default-initialized
        for (auto& v : val)
            v = init;
    }

    void
    set(const char* key, auto const& v)
        requires std::constructible_from<Json::Value, decltype(v)>
    {
        for (auto& a : this->val)
            a[key] = v;
    }

    // Intentionally not using class enum here, MultivarJson is scope enough
    enum IsMemberResult : int { none = 0, some, all };

    [[nodiscard]] IsMemberResult
    isMember(const char* key) const
    {
        int count = 0;
        for (auto& a : this->val)
            if (a.isMember(key))
                count += 1;

        return (count == 0 ? none : (count < size ? some : all));
    }

    static constexpr struct visitor_t final
    {
        // integral_constant version, extra arguments
        template <
            typename Json,
            unsigned int Version,
            typename... Args,
            typename Fn>
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
            static_assert(
                valid(Version) && index(Version) >= 0 && index(Version) < size);
            return std::invoke(
                fn,
                json.val[index(Version)],
                version,
                std::forward<Args>(args)...);
        }

        // integral_constant version, Json only
        template <typename Json, unsigned int Version, typename Fn>
            requires std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(
            Json& json,
            std::integral_constant<unsigned int, Version> const,
            Fn fn) const -> std::invoke_result_t<Fn, decltype(json.val[0])>
        {
            static_assert(
                valid(Version) && index(Version) >= 0 && index(Version) < size);
            return std::invoke(fn, json.val[index(Version)]);
        }

        // unsigned int version, extra arguments
        template <
            typename Json,
            typename Version,
            typename... Args,
            typename Fn>
            requires(!some_integral_constant<Version>) &&
                        std::convertible_to<Version, unsigned> &&
                        std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(Json& json, Version version, Fn fn, Args&&... args) const
            -> std::
                invoke_result_t<Fn, decltype(json.val[0]), Version, Args&&...>
        {
            ASSERT(
                valid(version) && index(version) >= 0 && index(version) < size,
                "ripple::detail::MultiApiJson::operator<Args...>() : valid "
                "version");
            return std::invoke(
                fn,
                json.val[index(version)],
                version,
                std::forward<Args>(args)...);
        }

        // unsigned int version, Json only
        template <typename Json, typename Version, typename Fn>
            requires(!some_integral_constant<Version>) &&
                        std::convertible_to<Version, unsigned> &&
                        std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(Json& json, Version version, Fn fn) const
            -> std::invoke_result_t<Fn, decltype(json.val[0])>
        {
            ASSERT(
                valid(version) && index(version) >= 0 && index(version) < size,
                "ripple::detail::MultiApiJson::operator() : valid version");
            return std::invoke(fn, json.val[index(version)]);
        }
    } visitor = {};

    auto
    visit()
    {
        return [self = this](auto... args)
            requires requires {
                visitor(
                    std::declval<MultiApiJson&>(),
                    std::declval<decltype(args)>()...);
            }
        { return visitor(*self, std::forward<decltype(args)>(args)...); };
    }

    auto
    visit() const
    {
        return [self = this](auto... args)
            requires requires {
                visitor(
                    std::declval<MultiApiJson const&>(),
                    std::declval<decltype(args)>()...);
            }
        { return visitor(*self, std::forward<decltype(args)>(args)...); };
    }

    template <typename... Args>
    auto
    visit(Args... args)
        -> std::invoke_result_t<visitor_t, MultiApiJson&, Args...>
        requires(sizeof...(args) > 0) &&
        requires { visitor(*this, std::forward<decltype(args)>(args)...); }
    {
        return visitor(*this, std::forward<decltype(args)>(args)...);
    }

    template <typename... Args>
    auto
    visit(Args... args) const
        -> std::invoke_result_t<visitor_t, MultiApiJson const&, Args...>
        requires(sizeof...(args) > 0) &&
        requires { visitor(*this, std::forward<decltype(args)>(args)...); }
    {
        return visitor(*this, std::forward<decltype(args)>(args)...);
    }
};

}  // namespace detail

// Wrapper for Json for all supported API versions.
using MultiApiJson = detail::
    MultiApiJson<RPC::apiMinimumSupportedVersion, RPC::apiMaximumValidVersion>;

}  // namespace ripple

#endif
