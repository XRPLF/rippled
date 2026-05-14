/** @file
 *  Implements `doLedgerHeader`, the handler for the `ledger_header` JSON-RPC
 *  command. The command returns both the canonical binary encoding of the
 *  requested ledger's header and its JSON representation, enabling clients to
 *  cryptographically verify ledger integrity independent of the server.
 */

#include <xrpl/protocol/LedgerHeader.h>

#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

#include <memory>

namespace xrpl {

/** Handle the `ledger_header` JSON-RPC command (API v1 only).
 *
 *  Returns two representations of the same ledger header side by side:
 *
 *  - `ledger_data`: the canonical 118-byte binary encoding of the header
 *    (hex-encoded), serialized via `addRaw` with `includeHash = false`.
 *    This is the exact pre-image that, when prefixed with
 *    `HashPrefix::ledgerMaster` and hashed with SHA-512 half, yields the
 *    ledger's identity hash. Clients can use this to verify the server's
 *    claims without trusting it.
 *
 *  - `ledger` (JSON): the human-readable header fields produced by `addJson`
 *    with `options = 0` (header only; no transactions, no state objects).
 *    These fields are the server's interpretation and cannot be independently
 *    verified from the response alone — hence the trust caveat below.
 *
 *  The ledger is resolved from `context.params` by `RPC::lookupLedger`,
 *  accepting `ledger_hash` (256-bit hex), `ledger_index` (integer), or
 *  shortcut strings (`"current"`, `"closed"`, `"validated"`). Malformed or
 *  missing parameters are handled entirely by `lookupLedger`; no input
 *  validation lives in this function.
 *
 *  @param context JSON-RPC context; `context.params` must supply exactly one
 *      ledger selector: `ledger_hash`, `ledger_index`, or the deprecated
 *      `ledger` field.
 *  @return A `Json::Value` containing `ledger_data` (hex binary), `ledger`
 *      (JSON header fields), and ledger identifying fields on success; an
 *      error object (`rpcLGR_NOT_FOUND`, `rpcINVALID_PARAMS`, etc.) on
 *      failure.
 *  @note This command is restricted to API v1. API v2 clients receive
 *      `rpcUNKNOWN_COMMAND`. Use the `ledger` command for v2-compatible
 *      header access.
 *  @note The JSON fields in `ledger` are not self-verifiable. Only
 *      `ledger_data` provides a trust anchor for cryptographic verification.
 *  @see addRaw, addJson, RPC::lookupLedger
 */
json::Value
doLedgerHeader(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    Serializer s;
    addRaw(lpLedger->header(), s);
    jvResult[jss::ledger_data] = strHex(s.peekData());

    // The JSON fields below are the server's interpretation of the header and
    // cannot be independently re-verified from this response alone.
    addJson(jvResult, {*lpLedger, &context, 0});

    return jvResult;
}

}  // namespace xrpl
