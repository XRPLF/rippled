#pragma once

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/serialize.h>

namespace xrpl {

struct LedgerFill
{
    LedgerFill(
        ReadView const& l,
        RPC::Context const* ctx,
        int o = 0,
        std::vector<TxQ::TxDetails> q = {})
        : ledger(l), options(o), txQueue(std::move(q)), context(ctx)
    {
        if (context != nullptr)
            closeTime = context->ledgerMaster.getCloseTimeBySeq(ledger.seq());
    }

    enum class Options {
        DumpTxrp = 1,
        DumpState = 2,
        Expand = 4,
        Full = 8,
        Binary = 16,
        OwnerFunds = 32,
        DumpQueue = 64
    };

    ReadView const& ledger;
    int options;
    std::vector<TxQ::TxDetails> txQueue;
    RPC::Context const* context;
    std::optional<NetClock::time_point> closeTime;
};

/** Given a Ledger and options, fill a json::Value with a
    description of the ledger.
 */
void
addJson(json::Value&, LedgerFill const&);

/** Return a new json::Value representing the ledger with given options.*/
json::Value
getJson(LedgerFill const&);

/** Copy all the keys and values from one object into another. */
void
copyFrom(json::Value& to, json::Value const& from);

}  // namespace xrpl
