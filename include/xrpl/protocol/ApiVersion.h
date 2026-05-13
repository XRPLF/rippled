#pragma once

#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <type_traits>
#include <utility>

/**
 * @file ApiVersion.h
 * @brief Single source of truth for the XRPL RPC API versioning scheme.
 *
 * Defines the compile-time integer constants that bound the accepted API
 * version range, JSON parsing and serialization helpers that enforce those
 * bounds at the RPC ingress point, and compile-time iteration templates
 * (`forApiVersions`, `forAllApiVersions`) that let the rest of the codebase
 * generate version-aware code paths without runtime switches.
 *
 * The versioning constants dictate the size and index mapping of every
 * `MultiApiJson` array in the system — changing them automatically adjusts
 * every data structure that stores per-version output.
 */

namespace xrpl {

namespace RPC {

/**
 * @brief Typed version-constant factory.
 *
 * Produces an `std::integral_constant<unsigned, Version>` tag for the given
 * version number. Using a distinct type per version allows overload resolution
 * and `if constexpr` branching at compile time while still implicitly decaying
 * to `unsigned` in arithmetic and comparison contexts.
 *
 * @tparam Version The API version number to encode as a type.
 */
template <unsigned int Version>
constexpr static std::integral_constant<unsigned, Version> kAPI_VERSION = {};

/** Sentinel returned by `getAPIVersionNumber()` when parsing fails or the
 *  supplied version falls outside the supported range. Callers that receive
 *  this value must reject the request before any handler dispatch. */
constexpr static auto kAPI_INVALID_VERSION = kAPI_VERSION<0>;

/** Oldest API version still accepted from network clients. Requests with a
 *  lower version are rejected; the floor advances when old versions are
 *  retired. */
constexpr static auto kAPI_MINIMUM_SUPPORTED_VERSION = kAPI_VERSION<1>;

/** Newest stable API version. Network requests are capped here unless the
 *  `[beta_rpc_api]` configuration flag is set, in which case
 *  `kAPI_BETA_VERSION` becomes the effective ceiling. */
constexpr static auto kAPI_MAXIMUM_SUPPORTED_VERSION = kAPI_VERSION<2>;

/** Implicit version assigned when a request omits the `api_version` field.
 *  Fixed at 1 because any request at version 2 or above must carry an
 *  explicit field; omitting it is treated as a version-1 request rather than
 *  an error. This constant will fall below `kAPI_MINIMUM_SUPPORTED_VERSION`
 *  once version-1 support is retired. */
constexpr static auto kAPI_VERSION_IF_UNSPECIFIED = kAPI_VERSION<1>;

/** Version used for command-line invocations.
 *  @note TODO: bump to 2 in a future release. */
constexpr static auto kAPI_COMMAND_LINE_VERSION = kAPI_VERSION<1>;

/** Experimental version gated behind the `[beta_rpc_api]` configuration flag.
 *  Completely invisible to clients connecting to a production node that has
 *  not opted in. */
constexpr static auto kAPI_BETA_VERSION = kAPI_VERSION<3>;

/** Absolute ceiling for template range loops; always equal to
 *  `kAPI_BETA_VERSION`. Drives the size of `MultiApiJson` arrays and the
 *  upper bound of `forAllApiVersions`. */
constexpr static auto kAPI_MAXIMUM_VALID_VERSION = kAPI_BETA_VERSION;

// --- Version-range invariants (load-bearing; update assertions when bumping
//     any constant above) ---
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

/**
 * @brief Populate the `version` sub-object in an RPC response.
 *
 * The output format diverges by negotiated version to maintain backwards
 * compatibility:
 * - **Version 1** (legacy): emits `first`, `good`, and `last` as semver
 *   strings (e.g. `"1.0.0"`). Static `SemanticVersion` objects are used to
 *   avoid repeated string parsing on every call.
 * - **Version 2+**: emits `first` as the minimum supported version integer
 *   and `last` as either `kAPI_BETA_VERSION` or `kAPI_MAXIMUM_SUPPORTED_VERSION`
 *   depending on `betaEnabled`.
 *
 * The primary consumer is `VersionHandler` in
 * `src/xrpld/rpc/handlers/server_info/Version.h`.
 *
 * @param parent      The JSON object into which the `version` key is written.
 * @param apiVersion  The negotiated API version for the current request; must
 *                    not be `kAPI_INVALID_VERSION`.
 * @param betaEnabled Whether the `[beta_rpc_api]` configuration flag is set,
 *                    which extends the reported `last` version to include the
 *                    beta version.
 */
inline void
setVersion(json::Value& parent, unsigned int apiVersion, bool betaEnabled)
{
    XRPL_ASSERT(apiVersion != kAPI_INVALID_VERSION, "xrpl::RPC::setVersion : input is valid");

    auto& retObj = parent[jss::version] = json::ValueType::Object;

    if (apiVersion == kAPI_VERSION_IF_UNSPECIFIED)
    {
        // Legacy semver-string format required by API version 1 clients.
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
 * @brief Extract and validate the API version from an incoming RPC request.
 *
 * Called at the RPC ingress point (`ServerHandler.cpp`) on every HTTP and
 * WebSocket request before handler dispatch. The function inspects the
 * top-level `api_version` field of `jv`:
 * - If the field is absent, returns `kAPI_VERSION_IF_UNSPECIFIED`.
 * - If the field is present but not an integer, returns `kAPI_INVALID_VERSION`.
 * - If the integer value falls outside
 *   `[kAPI_MINIMUM_SUPPORTED_VERSION, maxVersion]`, returns
 *   `kAPI_INVALID_VERSION`.
 * - Otherwise returns the integer value directly.
 *
 * Callers must treat a `kAPI_INVALID_VERSION` return as a signal to reject
 * the request immediately with an appropriate error.
 *
 * @param jv          The top-level JSON object of the incoming request.
 * @param betaEnabled When `false`, the effective ceiling is
 *                    `kAPI_MAXIMUM_SUPPORTED_VERSION`; when `true`, the ceiling
 *                    extends to `kAPI_BETA_VERSION`. Reflects the
 *                    `BETA_RPC_API` configuration flag of the serving node.
 * @return The negotiated API version, or `kAPI_INVALID_VERSION` if the
 *         request must be rejected.
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

/**
 * @brief Invoke a callable once for each API version in `[MinVer, MaxVer]`,
 *        passing the version as a distinct `std::integral_constant` type.
 *
 * The range is expanded into a parameter pack at compile time via
 * `std::make_index_sequence`, and the callable is called once per version in
 * order. Because each invocation receives a different type
 * (`std::integral_constant<unsigned, N>`), the callable may use
 * `if constexpr (Version >= 2)` to eliminate dead branches at compile time
 * rather than relying on a runtime switch.
 *
 * The C++20 `requires` clause enforces three constraints statically:
 * - `MaxVer >= MinVer` (non-empty range),
 * - `MinVer >= kAPI_MINIMUM_SUPPORTED_VERSION` (floor bound),
 * - `MaxVer <= kAPI_MAXIMUM_VALID_VERSION` (ceiling bound).
 * A caller that attempts to iterate outside the known valid range fails to
 * compile rather than producing a runtime out-of-bounds error.
 *
 * @note The `NOLINTBEGIN/NOLINTEND` block suppresses a spurious
 *       `bugprone-use-after-move` warning that clang-tidy raises on the fold
 *       expression when `Args` contains move-only types; the fold is safe
 *       because perfect-forwarding within a comma-expression does not actually
 *       move from the same argument twice.
 *
 * @tparam MinVer  First version in the iteration range (inclusive).
 * @tparam MaxVer  Last version in the iteration range (inclusive).
 * @tparam Fn      Callable type; must be invocable with
 *                 `(std::integral_constant<unsigned, V>, Args&&...)` for
 *                 every `V` in `[MinVer, MaxVer]`.
 * @tparam Args    Additional arguments forwarded verbatim to each invocation.
 * @param fn       The callable to invoke for each version.
 * @param args     Additional arguments forwarded to each invocation of `fn`.
 */
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

/**
 * @brief Invoke a callable once for every supported API version
 *        (`[kAPI_MINIMUM_SUPPORTED_VERSION, kAPI_MAXIMUM_VALID_VERSION]`).
 *
 * Thin wrapper around `forApiVersions` that fixes the range to the full set
 * of known versions (currently 1–3). This is the standard way to:
 * - Run a test scenario against every version in CI.
 * - Populate all slots of a `MultiApiJson` fan-out in a single pass (e.g.
 *   `NetworkOPs.cpp` uses it to build per-subscriber data when notifying of
 *   new transactions, calling `insertDeliverMax` only for versions where that
 *   field is defined).
 *
 * Each invocation of `fn` receives a distinct
 * `std::integral_constant<unsigned, N>` type for the version, enabling
 * compile-time branching inside the lambda body.
 *
 * @tparam Fn    Callable type; must satisfy the constraints of
 *               `forApiVersions` for the full version range.
 * @tparam Args  Additional arguments forwarded verbatim to each invocation.
 * @param fn     The callable to invoke for each version.
 * @param args   Additional arguments forwarded to each invocation of `fn`.
 */
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
