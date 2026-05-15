#pragma once

#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <type_traits>
#include <utility>

namespace xrpl {

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
static constexpr std::integral_constant<unsigned, Version> kApiVersion = {};

static constexpr auto kApiInvalidVersion = kApiVersion<0>;
static constexpr auto kApiMinimumSupportedVersion = kApiVersion<1>;
static constexpr auto kApiMaximumSupportedVersion = kApiVersion<2>;
static constexpr auto kApiVersionIfUnspecified = kApiVersion<1>;
static constexpr auto kApiCommandLineVersion = kApiVersion<1>;  // TODO Bump to 2 later
static constexpr auto kApiBetaVersion = kApiVersion<3>;
static constexpr auto kApiMaximumValidVersion = kApiBetaVersion;

static_assert(kApiInvalidVersion < kApiMinimumSupportedVersion);
static_assert(
    kApiVersionIfUnspecified >= kApiMinimumSupportedVersion &&
    kApiVersionIfUnspecified <= kApiMaximumSupportedVersion);
static_assert(
    kApiCommandLineVersion >= kApiMinimumSupportedVersion &&
    kApiCommandLineVersion <= kApiMaximumSupportedVersion);
static_assert(kApiMaximumSupportedVersion >= kApiMinimumSupportedVersion);
static_assert(kApiBetaVersion >= kApiMaximumSupportedVersion);
static_assert(kApiMaximumValidVersion >= kApiMaximumSupportedVersion);

inline void
setVersion(json::Value& parent, unsigned int apiVersion, bool betaEnabled)
{
    XRPL_ASSERT(apiVersion != kApiInvalidVersion, "xrpl::RPC::setVersion : input is valid");

    auto& retObj = parent[jss::version] = json::ValueType::Object;

    if (apiVersion == kApiVersionIfUnspecified)
    {
        // API version numbers used in API version 1
        static beast::SemanticVersion const kFirstVersion{"1.0.0"};
        static beast::SemanticVersion const kGoodVersion{"1.0.0"};
        static beast::SemanticVersion const kLastVersion{"1.0.0"};

        retObj[jss::first] = kFirstVersion.print();
        retObj[jss::good] = kGoodVersion.print();
        retObj[jss::last] = kLastVersion.print();
    }
    else
    {
        retObj[jss::first] = kApiMinimumSupportedVersion.value;
        retObj[jss::last] = betaEnabled ? kApiBetaVersion : kApiMaximumSupportedVersion;
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
 * @param jv a Json value that may or may not specify
 *        the api version number
 * @param betaEnabled if the beta API version is enabled
 * @return the api version number
 */
inline unsigned int
getAPIVersionNumber(json::Value const& jv, bool betaEnabled)
{
    static json::Value const kMinVersion(RPC::kApiMinimumSupportedVersion);
    json::Value const maxVersion(
        betaEnabled ? RPC::kApiBetaVersion : RPC::kApiMaximumSupportedVersion);

    if (jv.isObject())
    {
        if (jv.isMember(jss::api_version))
        {
            auto const specifiedVersion = jv[jss::api_version];
            if (!specifiedVersion.isInt() && !specifiedVersion.isUInt())
            {
                return RPC::kApiInvalidVersion;
            }
            auto const specifiedVersionInt = specifiedVersion.asInt();
            if (specifiedVersionInt < kMinVersion || specifiedVersionInt > maxVersion)
            {
                return RPC::kApiInvalidVersion;
            }
            return specifiedVersionInt;
        }
    }

    return RPC::kApiVersionIfUnspecified;
}

}  // namespace RPC

template <unsigned MinVer, unsigned MaxVer, typename Fn, typename... Args>
void
forApiVersions(Fn const& fn, Args&&... args)
    requires                                         //
    (MaxVer >= MinVer) &&                            //
    (MinVer >= RPC::kApiMinimumSupportedVersion) &&  //
    (RPC::kApiMaximumValidVersion >= MaxVer) && requires {
        fn(std::integral_constant<unsigned int, MinVer>{}, std::forward<Args>(args)...);
        fn(std::integral_constant<unsigned int, MaxVer>{}, std::forward<Args>(args)...);
    }
{
    static constexpr auto kSize = MaxVer + 1 - MinVer;
    [&]<std::size_t... Offset>(std::index_sequence<Offset...>) {
        // NOLINTBEGIN(bugprone-use-after-move)
        (((void)fn(
             std::integral_constant<unsigned int, MinVer + Offset>{}, std::forward<Args>(args)...)),
         ...);
        // NOLINTEND(bugprone-use-after-move)
    }(std::make_index_sequence<kSize>{});
}

template <typename Fn, typename... Args>
void
forAllApiVersions(Fn const& fn, Args&&... args)
    requires requires {
        forApiVersions<RPC::kApiMinimumSupportedVersion, RPC::kApiMaximumValidVersion>(
            fn, std::forward<Args>(args)...);
    }
{
    forApiVersions<RPC::kApiMinimumSupportedVersion, RPC::kApiMaximumValidVersion>(
        fn, std::forward<Args>(args)...);
}

}  // namespace xrpl
