#ifndef XRPL_APP_LEDGER_LEDGERTOJSON_H_INCLUDED
#define XRPL_APP_LEDGER_LEDGERTOJSON_H_INCLUDED

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/json/Object.h>
#include <xrpl/protocol/serialize.h>

namespace ripple {

struct LedgerFill
{
    LedgerFill(
        ReadView const& l,
        RPC::Context* ctx,
        int o = 0,
        std::vector<TxQ::TxDetails> q = {})
        : ledger(l), options(o), txQueue(std::move(q)), context(ctx)
    {
        if (context)
            closeTime = context->ledgerMaster.getCloseTimeBySeq(ledger.seq());
    }

    enum Options {
        dumpTxrp = 1,
        dumpState = 2,
        expand = 4,
        full = 8,
        binary = 16,
        ownerFunds = 32,
        dumpQueue = 64
    };

    ReadView const& ledger;
    int options;
    std::vector<TxQ::TxDetails> txQueue;
    RPC::Context* context;
    std::optional<NetClock::time_point> closeTime;
};

/** Given a Ledger and options, fill a Json::Object or Json::Value with a
    description of the ledger.
 */

void
addJson(Json::Value&, LedgerFill const&);

/** Return a new Json::Value representing the ledger with given options.*/
Json::Value
getJson(LedgerFill const&);

}  // namespace ripple

#endif
