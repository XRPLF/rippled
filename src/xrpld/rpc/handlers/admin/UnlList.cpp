/** @file
 *  Implements the `unl_list` admin RPC handler.
 *
 *  Reports a point-in-time snapshot of every validator key on the node's
 *  Unique Node List (UNL), together with a boolean indicating whether each
 *  key is currently *trusted* for consensus purposes.  A key is *listed*
 *  when it appears in at least one configured publisher list; it is *trusted*
 *  only when it additionally satisfies the overlap and quorum thresholds
 *  computed by `ValidatorList`.  Both dimensions are surfaced so operators
 *  can distinguish a healthy UNL from misconfigured or stale publisher lists.
 *
 *  Registered as `{"unl_list", byRef(&doUnlList), Role::ADMIN, NO_CONDITION}`.
 *  The `NO_CONDITION` flag means no ledger state or network connection is
 *  required — the UNL is a node-local configuration concept.
 *
 *  @see doValidators, doValidatorListSites for richer validator diagnostics.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <utility>

namespace xrpl {

/** Snapshot the UNL and emit each validator's base58 public key and trust
 *  status.
 *
 *  Delegates entirely to `ValidatorList::forEachListed`, which acquires a
 *  shared lock on the validator list before iterating, guaranteeing a
 *  consistent snapshot even if a background publisher-list refresh is in
 *  progress.  Each key is encoded with `toBase58(TokenType::NodePublic, ...)`
 *  — the standard Node Public representation used throughout XRPL tooling.
 *
 *  @param context  RPC dispatch context; `context.app.getValidators()` is
 *      the sole source of data.
 *  @return JSON object containing a `"unl"` array; each element has
 *      `"pubkey_validator"` (base58 Node Public key) and `"trusted"` (bool).
 *  @note This handler performs no input validation and has no error paths.
 *      Authentication and request parsing are handled by the RPC framework
 *      before dispatch.
 */
json::Value
doUnlList(RPC::JsonContext& context)
{
    json::Value obj(json::ValueType::Object);

    context.app.getValidators().forEachListed(
        [&unl = obj[jss::unl]](PublicKey const& publicKey, bool trusted) {
            json::Value node(json::ValueType::Object);

            node[jss::pubkey_validator] = toBase58(TokenType::NodePublic, publicKey);
            node[jss::trusted] = trusted;

            unl.append(std::move(node));
        });

    return obj;
}

}  // namespace xrpl
