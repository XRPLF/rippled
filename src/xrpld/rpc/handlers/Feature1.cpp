#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   feature : <feature>
//   vetoed : true/false
// }
Json::Value
doFeature(RPC::JsonContext& context)
{
    if (context.params.isMember(jss::feature))
    {
        // ensure that the `feature` param is a string
        if (!context.params[jss::feature].isString())
        {
            return rpcError(rpcINVALID_PARAMS);
        }
    }

    bool const isAdmin = context.role == Role::ADMIN;
    // Get majority amendment status
    majorityAmendments_t majorities;

    if (auto const valLedger = context.ledgerMaster.getValidatedLedger())
        majorities = getMajorityAmendments(*valLedger);

    auto& table = context.app.getAmendmentTable();

    if (!context.params.isMember(jss::feature))
    {
        auto features = table.getJson(isAdmin);

        for (auto const& [h, t] : majorities)
        {
            features[to_string(h)][jss::majority] =
                t.time_since_epoch().count();
        }

        Json::Value jvReply = Json::objectValue;
        jvReply[jss::features] = features;
        return jvReply;
    }

    auto feature = table.find(context.params[jss::feature].asString());

    // If the feature is not found by name, try to parse the `feature` param as
    // a feature ID. If that fails, return an error.
    if (!feature && !feature.parseHex(context.params[jss::feature].asString()))
        return rpcError(rpcBAD_FEATURE);

    if (context.params.isMember(jss::vetoed))
    {
        if (!isAdmin)
            return rpcError(rpcNO_PERMISSION);

        if (context.params[jss::vetoed].asBool())
            table.veto(feature);
        else
            table.unVeto(feature);
    }

    Json::Value jvReply = table.getJson(feature, isAdmin);
    if (!jvReply)
        return rpcError(rpcBAD_FEATURE);

    auto m = majorities.find(feature);
    if (m != majorities.end())
        jvReply[jss::majority] = m->second.time_since_epoch().count();

    return jvReply;
}

}  // namespace ripple
