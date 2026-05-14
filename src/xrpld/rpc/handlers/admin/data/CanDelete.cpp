/** @file
 *  Admin RPC handler for the `can_delete` command.
 *
 *  Lets node operators read or advance the online-deletion floor when the node
 *  is running in advisory-delete mode (`advisory_delete=1` in `[node_db]`).
 *  In that mode the `SHAMapStore` background thread will not prune history
 *  beyond the approved sequence number without an explicit operator command.
 */

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/case_conv.hpp>

#include <cstdint>
#include <limits>
#include <string>

namespace xrpl {

/** Query or update the online-deletion floor for an advisory-delete node.
 *
 *  If the `can_delete` parameter is absent the current threshold is returned.
 *  If the parameter is present it is parsed, the threshold is updated via
 *  `SHAMapStore::setCanDelete`, and the resulting value is echoed back so
 *  the caller can confirm the new state in a single round-trip.
 *
 *  The parameter accepts several representations (all string keywords are
 *  matched case-insensitively):
 *  - **unsigned integer** or **all-digit string** — used directly as a ledger
 *      sequence number; `beast::lexicalCast` throws on overflow.
 *  - `"never"` — maps to sequence `0`; pauses pruning without disabling the
 *      advisory-delete feature.
 *  - `"always"` — maps to `std::numeric_limits<uint32_t>::max()`; authorises
 *      the store to prune as aggressively as its configuration allows.
 *  - `"now"` — maps to `SHAMapStore::getLastRotated()`; returns
 *      `rpcNOT_READY` if no rotation has completed yet (value would be 0).
 *  - **64-hex-character ledger hash** — resolved to a sequence number via
 *      `LedgerMaster::getLedgerByHash`; returns `rpcLGR_NOT_FOUND` if the
 *      ledger is not available locally.
 *
 *  @param context  RPC dispatch context carrying params, app, and ledgerMaster.
 *  @return JSON object with a `can_delete` field holding the active threshold,
 *      or an RPC error object if advisory delete is not enabled, the node has
 *      not yet completed its first rotation, the ledger hash is unknown, or
 *      the parameter value cannot be parsed.
 *  @note Returns `rpcNOT_ENABLED` immediately when the node was not configured
 *      with `advisory_delete=1`; the command is a no-op on automatic-delete
 *      nodes and exposing it there would give operators a false sense of
 *      control.
 */
json::Value
doCanDelete(RPC::JsonContext& context)
{
    if (!context.app.getSHAMapStore().advisoryDelete())
        return RPC::makeError(RpcNotEnabled);

    json::Value ret(json::ValueType::Object);

    if (context.params.isMember(jss::can_delete))
    {
        json::Value const canDelete = context.params.get(jss::can_delete, 0);
        std::uint32_t canDeleteSeq = 0;

        if (canDelete.isUInt())
        {
            canDeleteSeq = canDelete.asUInt();
        }
        else
        {
            std::string canDeleteStr = canDelete.asString();
            boost::to_lower(canDeleteStr);

            if (canDeleteStr.find_first_not_of("0123456789") == std::string::npos)
            {
                canDeleteSeq = beast::lexicalCast<std::uint32_t>(canDeleteStr);
            }
            else if (canDeleteStr == "never")
            {
                canDeleteSeq = 0;
            }
            else if (canDeleteStr == "always")
            {
                canDeleteSeq = std::numeric_limits<std::uint32_t>::max();
            }
            else if (canDeleteStr == "now")
            {
                canDeleteSeq = context.app.getSHAMapStore().getLastRotated();
                if (canDeleteSeq == 0u)
                    return RPC::makeError(RpcNotReady);
            }
            else if (uint256 lh; lh.parseHex(canDeleteStr))
            {
                auto ledger = context.ledgerMaster.getLedgerByHash(lh);

                if (!ledger)
                    return RPC::makeError(RpcLgrNotFound, "ledgerNotFound");

                canDeleteSeq = ledger->header().seq;
            }
            else
            {
                return RPC::makeError(RpcInvalidParams);
            }
        }

        ret[jss::can_delete] = context.app.getSHAMapStore().setCanDelete(canDeleteSeq);
    }
    else
    {
        ret[jss::can_delete] = context.app.getSHAMapStore().getCanDelete();
    }

    return ret;
}

}  // namespace xrpl
