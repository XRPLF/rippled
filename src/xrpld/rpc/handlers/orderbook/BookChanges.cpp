#include <xrpld/rpc/BookChanges.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep

#include <memory>

namespace xrpl {

json::Value
doBookChanges(rpc::JsonContext& context)
{
    std::shared_ptr<ReadView const> ledger;

    json::Value result = rpc::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;

    return rpc::computeBookChanges(ledger);
}

}  // namespace xrpl
