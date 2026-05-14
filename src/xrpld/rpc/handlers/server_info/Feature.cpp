#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

/** RPC handler for the `feature` command.
 *
 *  Exposes the XRPL amendment system to external clients and privileged
 *  operators. Operates in two modes depending on whether the `feature`
 *  parameter is present.
 *
 *  **List mode** (no `feature` param): returns a `features` object keyed by
 *  amendment hash, sourced from `AmendmentTable::getJson(isAdmin)` and
 *  augmented with majority timestamps read directly from the validated ledger's
 *  amendment SLE. Reading majority data from the ledger (rather than relying
 *  solely on the `AmendmentTable`'s internal vote tracking) is authoritative
 *  because the two can briefly diverge.
 *
 *  **Single-feature mode** (with `feature` param): resolves the identifier
 *  first as a human-readable amendment name via `AmendmentTable::find()`,
 *  then falls back to parsing it as a 256-bit hex hash. Returns
 *  `rpcBAD_FEATURE` if neither succeeds, or if the resolved hash is not
 *  known to the amendment table.
 *
 *  **Veto control** (admin only): when `vetoed` is also present, calls
 *  `AmendmentTable::veto()` or `AmendmentTable::unVeto()` to control whether
 *  the local validator votes for the amendment. Non-admin callers who supply
 *  `vetoed` receive `rpcNO_PERMISSION`. The handler is registered as
 *  `Role::USER`, so any client can invoke the read paths; only the mutation
 *  path is gated here by the explicit admin check.
 *
 *  Majority timestamps in the response are raw `NetClock::time_point` ticks
 *  — seconds since the Ripple epoch (2000-01-01 00:00:00 UTC), **not** Unix
 *  time. Callers must add the 946684800-second offset to convert to Unix time.
 *
 *  @param context The RPC dispatch context, including request params, role,
 *      app reference, and ledger master.
 *  @return A JSON object with a top-level `features` map (list mode) or a
 *      single-amendment object keyed by hash (single-feature mode), or an
 *      RPC error value on invalid input or insufficient permissions.
 *  @note The `feature` parameter must be a JSON string; passing a number,
 *      boolean, null, object, or array returns `rpcINVALID_PARAMS`.
 *  @see AmendmentTable, getMajorityAmendments
 */
json::Value
doFeature(RPC::JsonContext& context)
{
    if (context.params.isMember(jss::feature))
    {
        if (!context.params[jss::feature].isString())
        {
            return rpcError(RpcInvalidParams);
        }
    }

    bool const isAdmin = context.role == Role::ADMIN;
    majorityAmendments_t majorities;

    if (auto const valLedger = context.ledgerMaster.getValidatedLedger())
        majorities = getMajorityAmendments(*valLedger);

    auto& table = context.app.getAmendmentTable();

    if (!context.params.isMember(jss::feature))
    {
        auto features = table.getJson(isAdmin);

        for (auto const& [h, t] : majorities)
        {
            features[to_string(h)][jss::majority] = t.time_since_epoch().count();
        }

        json::Value jvReply = json::ValueType::Object;
        jvReply[jss::features] = features;
        return jvReply;
    }

    auto feature = table.find(context.params[jss::feature].asString());

    if (!feature && !feature.parseHex(context.params[jss::feature].asString()))
        return rpcError(RpcBadFeature);

    if (context.params.isMember(jss::vetoed))
    {
        if (!isAdmin)
            return rpcError(RpcNoPermission);

        if (context.params[jss::vetoed].asBool())
        {
            table.veto(feature);
        }
        else
        {
            table.unVeto(feature);
        }
    }

    json::Value jvReply = table.getJson(feature, isAdmin);
    if (!jvReply)
        return rpcError(RpcBadFeature);

    auto m = majorities.find(feature);
    if (m != majorities.end())
        jvReply[jss::majority] = m->second.time_since_epoch().count();

    return jvReply;
}

}  // namespace xrpl
