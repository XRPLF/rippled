#include <xrpld/rpc/BookChanges.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/ledger/ReadView.h>

#include <memory>

namespace xrpl {

json::Value
doBookChanges(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> ledger;

    json::Value result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;

    return RPC::computeBookChanges(ledger);
}

}  // namespace xrpl
