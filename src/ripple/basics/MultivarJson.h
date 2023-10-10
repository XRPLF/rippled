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

#ifndef RIPPLE_BASICS_MULTIVARJSON_H_INCLUDED
#define RIPPLE_BASICS_MULTIVARJSON_H_INCLUDED

#include <ripple/json/json_value.h>

#include <array>
#include <concepts>
#include <cstdlib>

namespace ripple {
template <std::size_t Size>
struct MultivarJson
{
    std::array<Json::Value, Size> val;
    constexpr static std::size_t size = Size;

    Json::Value const&
    select(auto&& selector) const
        requires std::same_as<std::size_t, decltype(selector())>
    {
        return val[selector()];
    }

    void
    set(const char* key,
        auto const&
            v) requires std::constructible_from<Json::Value, decltype(v)>
    {
        for (auto& a : this->val)
            a[key] = v;
    }
};

// Wrapper for Json for all supported API versions.
using MultiApiJson = MultivarJson<2>;

// Helper to create appropriate selector for indexing MultiApiJson by apiVersion
constexpr auto
apiVersionSelector(unsigned int apiVersion) noexcept
{
    return [apiVersion]() constexpr
    {
        // apiVersion <= 1 returns 0
        // apiVersion > 1  returns 1
        return static_cast<std::size_t>(apiVersion > 1);
    };
}

}  // namespace ripple

#endif
