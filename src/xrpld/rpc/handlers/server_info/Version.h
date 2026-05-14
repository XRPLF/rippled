#pragma once

#include <xrpl/protocol/ApiVersion.h>

namespace xrpl::RPC {

/** New-style class-based handler for the `version` RPC endpoint.
 *
 *  Allows API clients to discover which API versions a node supports before
 *  making substantive requests. One of only two class-based ("new-style")
 *  handlers in the codebase — the other is `LedgerHandler`. All other RPC
 *  commands are registered as bare function pointers in the legacy
 *  `handlerArray` table in `Handler.cpp`. This class is registered via
 *  `HandlerTable::addHandler<VersionHandler>()` during singleton construction.
 *
 *  The new-style handler contract: expose a constructor taking `JsonContext&`,
 *  a static `check()` method, a `writeResult()` method, and the static
 *  constexpr metadata fields (`name`, `minApiVer`, `maxApiVer`, `role`,
 *  `condition`). The `handlerFrom<T>()` template bridges this shape into the
 *  `Handler` struct used by the dispatch infrastructure.
 *
 *  `maxApiVer` is deliberately set to `kAPI_MAXIMUM_VALID_VERSION` (the beta
 *  ceiling) rather than `kAPI_MAXIMUM_SUPPORTED_VERSION`. This allows clients
 *  running against a beta-enabled node to reach this endpoint at API version 3
 *  and discover that the beta version is available — if the cap were lower, the
 *  `HandlerTable` lookup at version 3 would return `nullptr` before the client
 *  could query capabilities.
 *
 *  @note `role = USER` and `condition = NoCondition`: no admin credentials are
 *      required and network-sync/ledger-availability checks are bypassed. A node
 *      that is still syncing or amendment-blocked can still truthfully report the
 *      API versions it supports.
 *
 *  @see setVersion() in `ApiVersion.h` for the format of the emitted JSON.
 */
class VersionHandler
{
public:
    /** Construct the handler, capturing version context from the request.
     *
     *  Only `apiVersion` and the `BETA_RPC_API` config flag are needed; all
     *  other context fields are ignored by this handler.
     *
     *  @param c  The RPC dispatch context for the incoming request.
     */
    explicit VersionHandler(JsonContext& c)
        : apiVersion_(c.apiVersion), betaEnabled_(c.app.config().BETA_RPC_API)
    {
    }

    /** Validate the request prior to result generation.
     *
     *  Always succeeds — asking "what versions do you support?" is valid
     *  regardless of node state.
     *
     *  @return `Status::kOK` unconditionally.
     */
    static Status
    check()
    {
        return Status::kOK;
    }

    /** Populate the response object with supported API version information.
     *
     *  Delegates to `setVersion()`. For API version 1 callers, emits legacy
     *  semver strings (`first`, `good`, `last` all `"1.0.0"`). For version 2+
     *  callers, emits integer bounds: `first = kAPI_MINIMUM_SUPPORTED_VERSION`,
     *  `last = kAPI_BETA_VERSION` if beta is enabled, otherwise
     *  `kAPI_MAXIMUM_SUPPORTED_VERSION`.
     *
     *  @param obj  The JSON object into which the `version` sub-object is written.
     */
    void
    writeResult(json::Value& obj) const
    {
        setVersion(obj, apiVersion_, betaEnabled_);
    }

    // NOLINTBEGIN(readability-identifier-naming)
    /** RPC method name used for handler table registration and dispatch. */
    static constexpr char const* name = "version";

    /** Lowest API version that routes to this handler (inclusive). */
    static constexpr unsigned minApiVer = RPC::kAPI_MINIMUM_SUPPORTED_VERSION;

    /** Highest API version that routes to this handler (inclusive).
     *
     *  Set to `kAPI_MAXIMUM_VALID_VERSION` (the beta ceiling) so that clients
     *  can reach this discovery endpoint even when requesting at the beta
     *  version level.
     */
    static constexpr unsigned maxApiVer = RPC::kAPI_MAXIMUM_VALID_VERSION;

    /** Minimum caller role; `USER` means no admin credentials are required. */
    static constexpr Role role = Role::USER;

    /** Node-state prerequisite; `NoCondition` bypasses all sync/ledger checks. */
    static constexpr Condition condition = Condition::NoCondition;
    // NOLINTEND(readability-identifier-naming)

private:
    unsigned int apiVersion_;
    bool betaEnabled_;
};

}  // namespace xrpl::RPC
