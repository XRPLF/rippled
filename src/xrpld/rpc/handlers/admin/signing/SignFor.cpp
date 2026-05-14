#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

/** Contribute one co-signer's signature to a multi-signed transaction.
 *
 *  Implements the `sign_for` JSON-RPC command. Enforces the server's signing
 *  access policy, tags the request as a heavy resource burden, then delegates
 *  all cryptographic work — key derivation, signature computation over the
 *  serialized transaction, and injection of the resulting `Signer` entry into
 *  the `Signers` array — to `RPC::transactionSignFor`. The response always
 *  carries a `deprecated` field steering callers toward client-side tools.
 *
 *  Access is granted when either condition holds:
 *  - The caller holds `Role::ADMIN` (trusted local/credentialed connection), or
 *  - The server is explicitly configured with `[signing_support] = 1`.
 *  Both failing returns `rpcNOT_SUPPORTED`. This two-level design separates
 *  routing permission (set in the handler table as `Role::USER`) from feature
 *  permission (enforced here), so public nodes never act as signing oracles
 *  even if inadvertently exposed to the internet.
 *
 *  @param context  RPC dispatch envelope carrying `params` (must include
 *      `tx_json`, `account` identifying whose signature slot is being filled,
 *      and a key field: `secret`, `seed`, `seed_hex`, or `passphrase`),
 *      the negotiated API version, the caller's role, and a reference to
 *      the application.
 *  @return JSON object containing the updated `tx_json` (with the new `Signer`
 *      entry appended) and `tx_blob` on success, or an error object on
 *      failure. A `deprecated` field is always present on success.
 *  @note Unlike `doSign`, `fail_hard` is read via `asBool()` directly without
 *      an `isMember()` guard. `Json::Value` returns `false` for absent fields,
 *      so the result is correct, but the inconsistency with the sibling
 *      handler is worth noting for future maintenance.
 *  @note `tx_json` must carry pre-filled `Sequence` and `SigningPubKey` fields;
 *      `transactionSignFor` does not auto-fill them, unlike single-signing.
 *  @see RPC::transactionSignFor
 *  @see doSign
 */
json::Value
doSignFor(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN && !context.app.config().canSign())
    {
        return RPC::makeError(RpcNotSupported, "Signing is not supported by this server.");
    }

    context.loadType = Resource::kFEE_HEAVY_BURDEN_RPC;
    auto const failHard = context.params[jss::fail_hard].asBool();
    auto const failType = NetworkOPs::doFailHard(failHard);

    auto ret = RPC::transactionSignFor(
        context.params,
        context.apiVersion,
        failType,
        context.role,
        context.ledgerMaster.getValidatedLedgerAge(),
        context.app);

    ret[jss::deprecated] =
        "This command has been deprecated and will be "
        "removed in a future version of the server. Please "
        "migrate to a standalone signing tool.";
    return ret;
}

}  // namespace xrpl
