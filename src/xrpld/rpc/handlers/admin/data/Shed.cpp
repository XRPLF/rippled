#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/shamap/SHAMap.h>

#include <cstddef>

namespace xrpl {

// shed {enable: bool?, run: bool = true, min_depth: uint?}
//
// `enable` sets the process-wide shed gate that drives the sweep hook. When
// `run` is true and the gate was already on before this request, one shed pass
// runs on the validated ledger's state map and the response carries the
// TreeNodeCache counts before and after it.
json::Value
doShed(rpc::JsonContext& context)
{
    bool const run = context.params.isMember("run") ? context.params["run"].asBool() : true;

    // Readers take the shed guard only while the gate is on, so a pass is safe
    // only against descents that started after it went up. Enabling and
    // shedding in one call could free nodes under an unguarded descent.
    bool const wasEnabled = SHAMap::shedEnabled();

    if (context.params.isMember("enable"))
    {
        bool const enable = context.params["enable"].asBool();
        SHAMap::setShedEnabled(enable);
        JLOG(context.j.warn()) << "shed RPC: gate " << (enable ? "ENABLED" : "DISABLED") << " (was "
                               << (wasEnabled ? "enabled" : "disabled") << ")";
    }

    auto const treeNodeCache = context.app.getNodeFamily().getTreeNodeCache();
    int const cacheBefore = treeNodeCache->getCacheSize();
    int const trackBefore = treeNodeCache->getTrackSize();

    std::size_t dropped = 0;
    bool const ranPass = run && wasEnabled && SHAMap::shedEnabled();
    if (ranPass)
    {
        if (auto const validated = context.ledgerMaster.getValidatedLedger())
        {
            unsigned const minDepth = context.params.isMember("min_depth")
                ? context.params["min_depth"].asUInt()
                : static_cast<unsigned>(context.app.config().shedMinDepth);

            // shedCold changes which nodes are resident, not the map's content.
            auto& stateMap = const_cast<SHAMap&>(validated->stateMap());
            dropped = stateMap.shedCold(minDepth);
            JLOG(context.j.warn())
                << "shed RPC: immediate pass on ledger " << validated->header().seq << " dropped "
                << dropped << " (min_depth " << minDepth << ")";
        }
        else
        {
            JLOG(context.j.warn()) << "shed RPC: no validated ledger; pass skipped";
        }
    }
    else if (run)
    {
        JLOG(context.j.warn()) << "shed RPC: pass requested but skipped (gate was "
                               << (wasEnabled ? "enabled" : "disabled") << " before this call)";
    }

    json::Value ret(json::ValueType::Object);
    ret[jss::enabled] = SHAMap::shedEnabled();
    ret["ran_pass"] = ranPass;
    if (run && !ranPass)
        ret["warning"] =
            "shed pass skipped: gate must already be enabled before "
            "a pass can run safely; call again to run one";
    ret["dropped"] = static_cast<json::UInt>(dropped);
    ret["treenode_cache_before"] = cacheBefore;
    ret["treenode_track_before"] = trackBefore;
    ret[jss::treenode_cache_size] = treeNodeCache->getCacheSize();
    ret[jss::treenode_track_size] = treeNodeCache->getTrackSize();
    return ret;
}

}  // namespace xrpl
