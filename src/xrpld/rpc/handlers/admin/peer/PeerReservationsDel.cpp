#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/Handlers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <optional>

namespace xrpl {

/** Remove a peer reservation by node public key (admin: `peer_reservations_del`).
 *
 *  Deletes the reserved peer slot held by the node identified by `public_key`,
 *  freeing that slot to be filled by the general overlay pool. Both the
 *  in-memory registry and the backing SQL table are updated atomically under
 *  `PeerReservationTable`'s internal mutex, so the deletion is immediately
 *  visible to concurrent peer-connection logic and survives a restart.
 *
 *  Validation is performed in three layers before any state is mutated:
 *  1. Presence — `public_key` must be present in `params`.
 *  2. Type — `public_key` must be a JSON string.
 *  3. Cryptographic parse — `public_key` must decode as a NodePublic base58
 *     key via `parseBase58<PublicKey>(TokenType::NodePublic, ...)`. The
 *     `NodePublic` token type is required to prevent account keys or other
 *     base58-encoded types from being accepted.
 *
 *  If the key is valid but no reservation existed, the operation is a no-op
 *  and the response is an empty JSON object — the handler does not treat a
 *  missing reservation as an error, making it safe to call idempotently from
 *  scripts or retry loops.
 *
 *  @note Parameter extraction is duplicated from `doPeerReservationsAdd` by
 *      design. A shared helper returning `Json::Value` would copy whole error
 *      objects on failure; the right abstraction is an error monad
 *      (`std::expected`-style), used here as explicit early-returns until
 *      that abstraction is adopted.
 *
 *  @param context  RPC dispatch context carrying `params` and the live
 *      `Application` reference.
 *  @return A JSON object, optionally containing a `"previous"` field with
 *      the reservation that was removed (serialized by
 *      `PeerReservation::toJson()`). Returns an empty object `{}` when no
 *      reservation existed for the given key.
 */
json::Value
doPeerReservationsDel(RPC::JsonContext& context)
{
    auto const& params = context.params;

    if (!params.isMember(jss::public_key))
        return RPC::missingFieldError(jss::public_key);
    if (!params[jss::public_key].isString())
        return RPC::expectedFieldError(jss::public_key, "a string");

    std::optional<PublicKey> optPk =
        parseBase58<PublicKey>(TokenType::NodePublic, params[jss::public_key].asString());
    if (!optPk)
        return rpcError(RpcPublicMalformed);
    PublicKey const& nodeId = *optPk;

    auto const previous = context.app.getPeerReservations().erase(nodeId);

    json::Value result{json::ValueType::Object};
    if (previous)
    {
        result[jss::previous] = previous->toJson();
    }
    return result;
}

}  // namespace xrpl
