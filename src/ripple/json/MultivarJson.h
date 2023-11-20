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

#ifndef RIPPLE_JSON_MULTIVARJSON_H_INCLUDED
#define RIPPLE_JSON_MULTIVARJSON_H_INCLUDED

#include <ripple/json/json_value.h>

#include <array>
#include <cassert>
#include <concepts>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace ripple {
template <std::size_t Size>
struct MultivarJson
{
    std::array<Json::Value, Size> val = {};
    constexpr static std::size_t size = Size;

    explicit MultivarJson(Json::Value const& init = {})
    {
        if (init == Json::Value{})
            return;  // All elements are already default-initialized
        for (auto& v : val)
            v = init;
    }

    Json::Value const&
    select(auto&& selector) const
        requires std::same_as<std::size_t, decltype(selector())>
    {
        auto const index = selector();
        assert(index < size);
        return val[index];
    }

    void
    set(const char* key,
        auto const&
            v) requires std::constructible_from<Json::Value, decltype(v)>
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
};

// Wrapper for Json for all supported API versions.
using MultiApiJson = MultivarJson<3>;

/*

NOTE:

If a future API version change adds another possible format, change the size of
`MultiApiJson`, and update `apiVersionSelector()` to return the appropriate
selection value for the new `apiVersion` and higher.

The more different JSON formats we support, the more CPU cycles we need to
prepare JSON for different API versions e.g. when publishing streams to
`subscribe` clients. Hence it is desirable to keep MultiApiJson small and
instead fully deprecate and remove support for old API versions. For example, if
we removed support for API version 1 and added a different format for API
version 3, the `apiVersionSelector` would change to
`static_cast<std::size_t>(apiVersion > 2)`

Such hypothetical change should correspond with change in RPCHelpers.h
`apiMinimumSupportedVersion = 2;`

*/

// Helper to create appropriate selector for indexing MultiApiJson by apiVersion
constexpr auto
apiVersionSelector(unsigned int apiVersion) noexcept
{
    return [apiVersion]() constexpr
    {
        return static_cast<std::size_t>(
            apiVersion <= 1         //
                ? 0                 //
                : (apiVersion <= 2  //
                       ? 1          //
                       : 2));
    };
}

// Helper to execute a callback for every version. Want both min and max version
// provided explicitly, so user will know to do update `size` when they change
template <
    unsigned int minVer,
    unsigned int maxVer,
    std::size_t size,
    typename Fn>
    requires                                       //
    (maxVer >= minVer) &&                          //
    (size == maxVer + 1 - minVer) &&               //
    (apiVersionSelector(minVer)() == 0) &&         //
    (apiVersionSelector(maxVer)() + 1 == size) &&  //
    requires(Json::Value& json, Fn fn)
{
    fn(json, static_cast<unsigned int>(1));
}
void
visit(MultivarJson<size>& json, Fn fn)
{
    [&]<std::size_t... offset>(std::index_sequence<offset...>)
    {
        static_assert(((apiVersionSelector(minVer + offset)() >= 0) && ...));
        static_assert(((apiVersionSelector(minVer + offset)() < size) && ...));
        (fn(json.val[apiVersionSelector(minVer + offset)()], minVer + offset),
         ...);
    }
    (std::make_index_sequence<size>{});
}

}  // namespace ripple

#endif
