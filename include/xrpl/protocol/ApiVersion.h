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
constexpr static std::integral_constant<unsigned, Version> kAPI_VERSION = {};

constexpr static auto kAPI_INVALID_VERSION = kAPI_VERSION<0>;
constexpr static auto kAPI_MINIMUM_SUPPORTED_VERSION = kAPI_VERSION<1>;
constexpr static auto kAPI_MAXIMUM_SUPPORTED_VERSION = kAPI_VERSION<2>;
constexpr static auto kAPI_VERSION_IF_UNSPECIFIED = kAPI_VERSION<1>;
constexpr static auto kAPI_COMMAND_LINE_VERSION = kAPI_VERSION<1>;  // TODO Bump to 2 later
constexpr static auto kAPI_BETA_VERSION = kAPI_VERSION<3>;
constexpr static auto kAPI_MAXIMUM_VALID_VERSION = kAPI_BETA_VERSION;

static_assert(kAPI_INVALID_VERSION < kAPI_MINIMUM_SUPPORTED_VERSION);
static_assert(
    kAPI_VERSION_IF_UNSPECIFIED >= kAPI_MINIMUM_SUPPORTED_VERSION &&
    kAPI_VERSION_IF_UNSPECIFIED <= kAPI_MAXIMUM_SUPPORTED_VERSION);
static_assert(
    kAPI_COMMAND_LINE_VERSION >= kAPI_MINIMUM_SUPPORTED_VERSION &&
    kAPI_COMMAND_LINE_VERSION <= kAPI_MAXIMUM_SUPPORTED_VERSION);
static_assert(kAPI_MAXIMUM_SUPPORTED_VERSION >= kAPI_MINIMUM_SUPPORTED_VERSION);
static_assert(kAPI_BETA_VERSION >= kAPI_MAXIMUM_SUPPORTED_VERSION);
static_assert(kAPI_MAXIMUM_VALID_VERSION >= kAPI_MAXIMUM_SUPPORTED_VERSION);

inline void
setVersion(json::Value& parent, unsigned int apiVersion, bool betaEnabled)
{
    XRPL_ASSERT(apiVersion != kAPI_INVALID_VERSION, "xrpl::RPC::setVersion : input is valid");

    auto& retObj = parent[jss::version] = json::ValueType::Object;

    if (apiVersion == kAPI_VERSION_IF_UNSPECIFIED)
    {
        // API version numbers used in API version 1
        static beast::SemanticVersion const kFIRST_VERSION{"1.0.0"};
        static beast::SemanticVersion const kGOOD_VERSION{"1.0.0"};
        static beast::SemanticVersion const kLAST_VERSION{"1.0.0"};

        retObj[jss::first] = kFIRST_VERSION.print();
        retObj[jss::good] = kGOOD_VERSION.print();
        retObj[jss::last] = kLAST_VERSION.print();
    }
    else
    {
        retObj[jss::first] = kAPI_MINIMUM_SUPPORTED_VERSION.value;
        retObj[jss::last] = betaEnabled ? kAPI_BETA_VERSION : kAPI_MAXIMUM_SUPPORTED_VERSION;
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
    static json::Value const kMIN_VERSION(RPC::kAPI_MINIMUM_SUPPORTED_VERSION);
    json::Value const maxVersion(
        betaEnabled ? RPC::kAPI_BETA_VERSION : RPC::kAPI_MAXIMUM_SUPPORTED_VERSION);

    if (jv.isObject())
    {
        if (jv.isMember(jss::api_version))
        {
            auto const specifiedVersion = jv[jss::api_version];
            if (!specifiedVersion.isInt() && !specifiedVersion.isUInt())
            {
                return RPC::kAPI_INVALID_VERSION;
            }
            auto const specifiedVersionInt = specifiedVersion.asInt();
            if (specifiedVersionInt < kMIN_VERSION || specifiedVersionInt > maxVersion)
            {
                return RPC::kAPI_INVALID_VERSION;
            }
            return specifiedVersionInt;
        }
    }

    return RPC::kAPI_VERSION_IF_UNSPECIFIED;
}

}  // namespace RPC

template <unsigned MinVer, unsigned MaxVer, typename Fn, typename... Args>
void
forApiVersions(Fn const& fn, Args&&... args)
    requires                                            //
    (MaxVer >= MinVer) &&                               //
    (MinVer >= RPC::kAPI_MINIMUM_SUPPORTED_VERSION) &&  //
    (RPC::kAPI_MAXIMUM_VALID_VERSION >= MaxVer) && requires {
        fn(std::integral_constant<unsigned int, MinVer>{}, std::forward<Args>(args)...);
        fn(std::integral_constant<unsigned int, MaxVer>{}, std::forward<Args>(args)...);
    }
{
    constexpr auto kSIZE = MaxVer + 1 - MinVer;
    [&]<std::size_t... Offset>(std::index_sequence<Offset...>) {
        // NOLINTBEGIN(bugprone-use-after-move)
        (((void)fn(
             std::integral_constant<unsigned int, MinVer + Offset>{}, std::forward<Args>(args)...)),
         ...);
        // NOLINTEND(bugprone-use-after-move)
    }(std::make_index_sequence<kSIZE>{});
}

template <typename Fn, typename... Args>
void
forAllApiVersions(Fn const& fn, Args&&... args)
    requires requires {
        forApiVersions<RPC::kAPI_MINIMUM_SUPPORTED_VERSION, RPC::kAPI_MAXIMUM_VALID_VERSION>(
            fn, std::forward<Args>(args)...);
    }
{
    forApiVersions<RPC::kAPI_MINIMUM_SUPPORTED_VERSION, RPC::kAPI_MAXIMUM_VALID_VERSION>(
        fn, std::forward<Args>(args)...);
}

}  // namespace xrpl
