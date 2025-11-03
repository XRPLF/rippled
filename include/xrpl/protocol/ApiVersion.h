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

#ifndef XRPL_PROTOCOL_APIVERSION_H_INCLUDED
#define XRPL_PROTOCOL_APIVERSION_H_INCLUDED

#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <type_traits>
#include <utility>

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
constexpr static auto apiMinimumSupportedVersion = apiVersion<1>;
constexpr static auto apiMaximumSupportedVersion = apiVersion<2>;
constexpr static auto apiVersionIfUnspecified = apiVersion<1>;
constexpr static auto apiCommandLineVersion =
    apiVersion<1>;  // TODO Bump to 2 later
constexpr static auto apiBetaVersion = apiVersion<3>;
constexpr static auto apiMaximumValidVersion = apiBetaVersion;

static_assert(apiInvalidVersion < apiMinimumSupportedVersion);
static_assert(
    apiVersionIfUnspecified >= apiMinimumSupportedVersion &&
    apiVersionIfUnspecified <= apiMaximumSupportedVersion);
static_assert(
    apiCommandLineVersion >= apiMinimumSupportedVersion &&
    apiCommandLineVersion <= apiMaximumSupportedVersion);
static_assert(apiMaximumSupportedVersion >= apiMinimumSupportedVersion);
static_assert(apiBetaVersion >= apiMaximumSupportedVersion);
static_assert(apiMaximumValidVersion >= apiMaximumSupportedVersion);

template <class JsonObject>
void
setVersion(JsonObject& parent, unsigned int apiVersion, bool betaEnabled)
{
    XRPL_ASSERT(
        apiVersion != apiInvalidVersion,
        "ripple::RPC::setVersion : input is valid");
    auto& retObj = addObject(parent, jss::version);

    if (apiVersion == apiVersionIfUnspecified)
    {
        // API version numbers used in API version 1
        static beast::SemanticVersion const firstVersion{"1.0.0"};
        static beast::SemanticVersion const goodVersion{"1.0.0"};
        static beast::SemanticVersion const lastVersion{"1.0.0"};

        retObj[jss::first] = firstVersion.print();
        retObj[jss::good] = goodVersion.print();
        retObj[jss::last] = lastVersion.print();
    }
    else
    {
        retObj[jss::first] = apiMinimumSupportedVersion.value;
        retObj[jss::last] =
            betaEnabled ? apiBetaVersion : apiMaximumSupportedVersion;
    }
}

/**
 * Retrieve the api version number from the json value
 *
 * Note that APIInvalidVersion will be returned if
 * 1) the version number field has a wrong format
 * 2) the version number retrieved is out of the supported range
 * 3) the version number is unspecified and
 *    APIVersionIfUnspecified is out of the supported range
 *
 * @param jv a Json value that may or may not specifies
 *        the api version number
 * @param betaEnabled if the beta API version is enabled
 * @return the api version number
 */
inline unsigned int
getAPIVersionNumber(Json::Value const& jv, bool betaEnabled)
{
    static Json::Value const minVersion(RPC::apiMinimumSupportedVersion);
    Json::Value const maxVersion(
        betaEnabled ? RPC::apiBetaVersion : RPC::apiMaximumSupportedVersion);

    if (jv.isObject())
    {
        if (jv.isMember(jss::api_version))
        {
            auto const specifiedVersion = jv[jss::api_version];
            if (!specifiedVersion.isInt() && !specifiedVersion.isUInt())
            {
                return RPC::apiInvalidVersion;
            }
            auto const specifiedVersionInt = specifiedVersion.asInt();
            if (specifiedVersionInt < minVersion ||
                specifiedVersionInt > maxVersion)
            {
                return RPC::apiInvalidVersion;
            }
            return specifiedVersionInt;
        }
    }

    return RPC::apiVersionIfUnspecified;
}

}  // namespace RPC

template <unsigned minVer, unsigned maxVer, typename Fn, typename... Args>
void
forApiVersions(Fn const& fn, Args&&... args)
    requires                                        //
    (maxVer >= minVer) &&                           //
    (minVer >= RPC::apiMinimumSupportedVersion) &&  //
    (RPC::apiMaximumValidVersion >= maxVer) && requires {
        fn(std::integral_constant<unsigned int, minVer>{},
           std::forward<Args>(args)...);
        fn(std::integral_constant<unsigned int, maxVer>{},
           std::forward<Args>(args)...);
    }
{
    constexpr auto size = maxVer + 1 - minVer;
    [&]<std::size_t... offset>(std::index_sequence<offset...>) {
        (((void)fn(
             std::integral_constant<unsigned int, minVer + offset>{},
             std::forward<Args>(args)...)),
         ...);
    }(std::make_index_sequence<size>{});
}

template <typename Fn, typename... Args>
void
forAllApiVersions(Fn const& fn, Args&&... args)
    requires requires {
        forApiVersions<
            RPC::apiMinimumSupportedVersion,
            RPC::apiMaximumValidVersion>(fn, std::forward<Args>(args)...);
    }
{
    forApiVersions<
        RPC::apiMinimumSupportedVersion,
        RPC::apiMaximumValidVersion>(fn, std::forward<Args>(args)...);
}

}  // namespace ripple

#endif
