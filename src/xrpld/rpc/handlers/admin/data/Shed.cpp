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
// Admin-only live control of sheddable resident subtrees. `enable` flips the
// process-wide runtime gate that drives the sweep hook. When `run` is true a
// single shed pass is executed immediately on the most-recent fully-validated
// ledger's state map, so the response carries an instant before/after.
json::Value
doShed(RPC::JsonContext& context)
{
    bool const run = context.params.isMember("run") ? context.params["run"].asBool() : true;

    if (context.params.isMember("enable"))
        SHAMap::setShedEnabled(context.params["enable"].asBool());

    auto const treeNodeCache = context.app.getNodeFamily().getTreeNodeCache();
    int const cacheBefore = treeNodeCache->getCacheSize();
    int const trackBefore = treeNodeCache->getTrackSize();

    std::size_t dropped = 0;
    if (run)
    {
        if (auto const validated = context.ledgerMaster.getValidatedLedger())
        {
            unsigned const minDepth = context.params.isMember("min_depth")
                ? context.params["min_depth"].asUInt()
                : static_cast<unsigned>(context.app.config().shedMinDepth);

            // shedCold mutates the shared physical tree; mirror the sweep hook's
            // const_cast off the const stateMap ref (the map stays logically
            // unchanged, dropped nodes re-fault from the NodeStore on demand).
            auto& stateMap = const_cast<SHAMap&>(validated->stateMap());
            dropped = stateMap.shedCold(minDepth);
        }
    }

    json::Value ret(json::ValueType::Object);
    ret[jss::enabled] = SHAMap::shedEnabled();
    ret["dropped"] = static_cast<json::UInt>(dropped);
    ret["treenode_cache_before"] = cacheBefore;
    ret["treenode_track_before"] = trackBefore;
    ret[jss::treenode_cache_size] = treeNodeCache->getCacheSize();
    ret[jss::treenode_track_size] = treeNodeCache->getTrackSize();
    return ret;
}

}  // namespace xrpl
