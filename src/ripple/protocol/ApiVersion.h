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

#ifndef RIPPLE_PROTOCOL_APIVERSION_H_INCLUDED
#define RIPPLE_PROTOCOL_APIVERSION_H_INCLUDED

#include <ripple/json/MultivarJson.h>

#include <functional>

namespace ripple {

/**
 * API version numbers used in later API versions
 *
 * Requests with a version number in the range
 * [apiMinimumSupportedVersion, apiMaximumSupportedVersion]
 * are supported.
 *
 * If [beta_rpc_api] is enabled in config, the version numbers
 * in the range [apiMinimumSupportedVersion, apiBetaVersion]
 * are supported.
 *
 * Network Requests without explicit version numbers use
 * apiVersionIfUnspecified. apiVersionIfUnspecified is 1,
 * because all the RPC requests with a version >= 2 must
 * explicitly specify the version in the requests.
 * Note that apiVersionIfUnspecified will be lower than
 * apiMinimumSupportedVersion when we stop supporting API
 * version 1.
 *
 * Command line Requests use apiCommandLineVersion.
 */

namespace RPC {

template <unsigned int Version>
constexpr static std::integral_constant<unsigned, Version> apiVersion = {};

constexpr static auto apiInvalidVersion = apiVersion<0>;
constexpr static auto apiVersionIfUnspecified = apiVersion<1>;
constexpr static auto apiMinimumSupportedVersion = apiVersion<1>;
constexpr static auto apiMaximumSupportedVersion = apiVersion<2>;
constexpr static auto apiCommandLineVersion =
    apiVersion<1>;  // TODO Bump to 2 later
constexpr static auto apiBetaVersion = apiVersion<3>;
constexpr static auto apiMaximumValidVersion = apiBetaVersion;

static_assert(apiMinimumSupportedVersion >= apiVersionIfUnspecified);
static_assert(apiMaximumSupportedVersion >= apiMinimumSupportedVersion);
static_assert(apiBetaVersion >= apiMaximumSupportedVersion);
static_assert(apiMaximumValidVersion >= apiMaximumSupportedVersion);

}  // namespace RPC

template <unsigned minVer, unsigned maxVer, typename Fn, typename... Args>
    requires(maxVer >= minVer) &&                   //
    (minVer >= RPC::apiMinimumSupportedVersion) &&  //
    (RPC::apiMaximumValidVersion >=
     maxVer) void forApiVersions(Fn const& fn, Args&&... args) requires requires
{
    fn(std::integral_constant<unsigned int, minVer>{},
       std::forward<Args>(args)...);
    fn(std::integral_constant<unsigned int, maxVer>{},
       std::forward<Args>(args)...);
}
{
    constexpr auto size = maxVer + 1 - minVer;
    [&]<std::size_t... offset>(std::index_sequence<offset...>)
    {
        (((void)fn(
             std::integral_constant<unsigned int, minVer + offset>{},
             std::forward<Args>(args)...)),
         ...);
    }
    (std::make_index_sequence<size>{});
}

template <typename Fn, typename... Args>
void
forAllApiVersions(Fn const& fn, Args&&... args) requires requires()
{
    forApiVersions<
        RPC::apiMinimumSupportedVersion,
        RPC::apiMaximumValidVersion>(fn, std::forward<Args>(args)...);
}
{
    forApiVersions<
        RPC::apiMinimumSupportedVersion,
        RPC::apiMaximumValidVersion>(fn, std::forward<Args>(args)...);
}

struct ApiVersionHelper final
{
    constexpr static std::size_t size =
        RPC::apiMaximumValidVersion + 1 - RPC::apiMinimumSupportedVersion;

    constexpr static auto
    index(unsigned int v) noexcept -> std::size_t
    {
        return static_cast<std::size_t>(v <= 1 ? 0 : (v <= 2 ? 1 : 2));
    }

    constexpr static auto
    valid(unsigned int v) noexcept -> bool
    {
        return v >= RPC::apiMinimumSupportedVersion &&
            v <= RPC::apiMaximumValidVersion;
    }
};

// Wrapper for Json for all supported API versions.
using MultiApiJson = MultivarJson<ApiVersionHelper>;
}  // namespace ripple

#endif
