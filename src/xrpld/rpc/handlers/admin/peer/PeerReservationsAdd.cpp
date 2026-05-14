#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/Handlers.h>

#include <xrpl/core/PeerReservationTable.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <optional>
#include <string>

namespace xrpl {

/** Insert or replace a peer reservation (admin: `peer_reservations_add`).
 *
 *  A peer reservation gives a specific XRPL node a guaranteed inbound
 *  connection slot in the overlay, even when the general pool is full.
 *  This handler is the write path: it validates the caller-supplied node
 *  public key, optionally records a human-readable description, and
 *  delegates to `PeerReservationTable::insertOrAssign` which updates both
 *  the in-memory registry and the backing SQL table under a single lock.
 *
 *  Validation is performed in three layers before any state is mutated:
 *  1. Presence — `public_key` must be present.
 *  2. Type — `public_key` (and `description`, if present) must be a string.
 *  3. Cryptographic parse — `public_key` must decode as a NodePublic base58
 *     key via `parseBase58<PublicKey>`. Only base58 is accepted (not hex),
 *     as a deliberate design choice to eliminate ambiguous-format bugs.
 *
 *  The response object is always a JSON `{}`. If a prior reservation existed
 *  for the same node key it is returned under `jss::previous` (serialized by
 *  `PeerReservation::toJson()`), giving callers idempotent-safe confirmation
 *  of what was displaced. A fresh insert returns an empty object with no
 *  `previous` field.
 *
 *  @note The inline parameter-extraction code intentionally avoids a shared
 *      helper that returns `Json::Value`. Copying whole JSON objects to
 *      propagate error codes is expensive and clutters call sites. The right
 *      abstraction is an error monad (`std::expected`-style
 *      `optional<T, Error>`); the code uses direct early-returns as the
 *      practical stand-in until such an abstraction is adopted.
 *
 *  @param context  RPC dispatch context carrying `params` and the live
 *      `Application` reference.
 *  @return A JSON object, optionally containing a `"previous"` field with
 *      the reservation that was replaced.
 *  @throws soci::soci_error  Propagated from `PeerReservationTable::insertOrAssign`
 *      if the underlying SQL write fails.
 */
json::Value
doPeerReservationsAdd(RPC::JsonContext& context)
{
    auto const& params = context.params;

    if (!params.isMember(jss::public_key))
        return RPC::missingFieldError(jss::public_key);

    if (!params[jss::public_key].isString())
        return RPC::expectedFieldError(jss::public_key, "a string");

    std::string desc;
    if (params.isMember(jss::description))
    {
        if (!params[jss::description].isString())
            return RPC::expectedFieldError(jss::description, "a string");
        desc = params[jss::description].asString();
    }

    std::optional<PublicKey> optPk =
        parseBase58<PublicKey>(TokenType::NodePublic, params[jss::public_key].asString());
    if (!optPk)
        return rpcError(RpcPublicMalformed);
    PublicKey const& nodeId = *optPk;

    auto const previous = context.app.getPeerReservations().insertOrAssign(
        PeerReservation{.nodeId = nodeId, .description = desc});

    json::Value result{json::ValueType::Object};
    if (previous)
    {
        result[jss::previous] = previous->toJson();
    }
    return result;
}

}  // namespace xrpl
