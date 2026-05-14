#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

/** Sign a transaction and return the signed blob without submitting it.
 *
 *  Implements the `sign` JSON-RPC command. Enforces the server's signing
 *  access policy, tags the request as a heavy resource burden, then delegates
 *  all cryptographic and structural work — key derivation, field auto-fill
 *  (`Fee`, `Sequence`), serialization, and ECDSA/Ed25519 signing — to
 *  `RPC::transactionSign`. The response always carries a `deprecated` field
 *  steering callers toward client-side signing tools.
 *
 *  Access is granted when either condition holds:
 *  - The caller holds `Role::ADMIN` (trusted local/credentialed connection), or
 *  - The server is explicitly configured with `[signing_support] = 1`.
 *  Both failing returns `rpcNOT_SUPPORTED`. The default configuration denies
 *  non-admin callers, so a server inadvertently exposed to the internet will
 *  not act as a signing oracle.
 *
 *  @param context  RPC dispatch envelope carrying `params` (must include
 *      `tx_json` and a key field: `secret`, `seed`, `seed_hex`, or
 *      `passphrase`), the negotiated API version, the caller's role, and
 *      a reference to the application.
 *  @return JSON object containing `tx_blob` and `tx_json` on success, or an
 *      error object on failure. A `deprecated` field is always present.
 *  @note `fail_hard` is read defensively via `isMember()` before `asBool()`
 *      to handle its absence safely; the flag is forwarded to
 *      `transactionSign` where it governs signing-pipeline error treatment.
 *  @note Signing is rejected if the most recently validated ledger is older
 *      than `Tuning::kMAX_VALIDATED_LEDGER_AGE` (2 minutes), preventing
 *      transactions from being built against stale ledger state.
 *  @see RPC::transactionSign
 *  @see doSignFor
 */
json::Value
doSign(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN && !context.app.config().canSign())
    {
        return RPC::makeError(RpcNotSupported, "Signing is not supported by this server.");
    }

    context.loadType = Resource::kFEE_HEAVY_BURDEN_RPC;
    NetworkOPs::FailHard const failType = NetworkOPs::doFailHard(
        context.params.isMember(jss::fail_hard) && context.params[jss::fail_hard].asBool());

    auto ret = RPC::transactionSign(
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
