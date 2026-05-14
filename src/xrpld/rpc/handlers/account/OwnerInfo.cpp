#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <optional>
#include <string>

namespace xrpl {

/** Handler for the `owner_info` RPC command.
 *
 *  Returns a compact snapshot of an account's owned ledger objects — open
 *  offers and trust lines — queried simultaneously against two ledger views:
 *  the most recently closed (validated) ledger (`accepted`) and the current
 *  open ledger (`current`). This dual-view allows callers to observe both
 *  settled state and pending changes not yet incorporated into a validated
 *  ledger.
 *
 *  This is a legacy endpoint predating `account_objects`. It is maintained
 *  for backward compatibility only; `account_objects` (with `marker`-based
 *  pagination) should be preferred for production use because `owner_info`
 *  traverses the entire owner directory twice per call without any pagination.
 *
 *  Registered with `NEEDS_CURRENT_LEDGER`, so the RPC framework rejects the
 *  call before it reaches this handler if no current ledger is available.
 *
 *  **Input fields:**
 *  - `account` — Base58Check-encoded account address (canonical name).
 *  - `ident` — Legacy alias for `account`; accepted for backward
 *      compatibility with older clients. If both are present, `account`
 *      takes precedence.
 *
 *  **Response shape:**
 *  ```json
 *  {
 *    "accepted": { "offers": [...], "ripple_lines": [...] },
 *    "current":  { "offers": [...], "ripple_lines": [...] }
 *  }
 *  ```
 *
 *  @param context The RPC dispatch context, providing request parameters,
 *      `ledgerMaster` for ledger access, and `netOps` for owner-directory
 *      traversal.
 *  @return A `Json::Value` object with `accepted` and `current` keys. If the
 *      account identifier is syntactically or semantically invalid (fails
 *      Base58Check decoding), both keys are set to an `rpcACT_MALFORMED`
 *      error object rather than returning a top-level error. If the `account`
 *      (or `ident`) field is absent entirely, a top-level
 *      `rpcINVALID_PARAMS` error is returned instead.
 *  @note A malformed account address produces `rpcACT_MALFORMED` under
 *      **both** `accepted` and `current` — not a single top-level error.
 *      Callers that inspect only one key may silently miss the error in the
 *      other. A non-existent (but well-formed) account yields empty `offers`
 *      and `ripple_lines` arrays with a `success` status rather than an error.
 */
json::Value
doOwnerInfo(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::account) && !context.params.isMember(jss::ident))
    {
        return RPC::missingFieldError(jss::account);
    }

    std::string const strIdent = context.params.isMember(jss::account)
        ? context.params[jss::account].asString()
        : context.params[jss::ident].asString();
    json::Value ret;

    auto const& closedLedger = context.ledgerMaster.getClosedLedger();
    std::optional<AccountID> const accountID = parseBase58<AccountID>(strIdent);
    ret[jss::accepted] = accountID.has_value()
        ? context.netOps.getOwnerInfo(closedLedger, accountID.value())
        : rpcError(RpcActMalformed);

    auto const& currentLedger = context.ledgerMaster.getCurrentLedger();
    ret[jss::current] = accountID.has_value()
        ? context.netOps.getOwnerInfo(currentLedger, *accountID)
        : rpcError(RpcActMalformed);
    return ret;
}

}  // namespace xrpl
