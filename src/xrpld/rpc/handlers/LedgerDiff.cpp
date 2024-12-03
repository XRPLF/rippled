#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

namespace ripple {
std::pair<org::xrpl::rpc::v1::GetLedgerDiffResponse, grpc::Status>
doLedgerDiffGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerDiffRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerDiffRequest& request = context.params;
    org::xrpl::rpc::v1::GetLedgerDiffResponse response;
    grpc::Status status = grpc::Status::OK;

    std::shared_ptr<ReadView const> baseLedgerRv;
    std::shared_ptr<ReadView const> desiredLedgerRv;

    if (RPC::ledgerFromSpecifier(baseLedgerRv, request.base_ledger(), context))
    {
        grpc::Status errorStatus{
            grpc::StatusCode::NOT_FOUND, "base ledger not found"};
        return {response, errorStatus};
    }

    if (RPC::ledgerFromSpecifier(
            desiredLedgerRv, request.desired_ledger(), context))
    {
        grpc::Status errorStatus{
            grpc::StatusCode::NOT_FOUND, "desired ledger not found"};
        return {response, errorStatus};
    }

    std::shared_ptr<Ledger const> baseLedger =
        std::dynamic_pointer_cast<Ledger const>(baseLedgerRv);
    if (!baseLedger)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::NOT_FOUND, "base ledger not validated"};
        return {response, errorStatus};
    }

    std::shared_ptr<Ledger const> desiredLedger =
        std::dynamic_pointer_cast<Ledger const>(desiredLedgerRv);
    if (!desiredLedger)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::NOT_FOUND, "base ledger not validated"};
        return {response, errorStatus};
    }

    SHAMap::Delta differences;

    int maxDifferences = std::numeric_limits<int>::max();

    bool res = baseLedger->stateMap().compare(
        desiredLedger->stateMap(), differences, maxDifferences);
    if (!res)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::RESOURCE_EXHAUSTED,
            "too many differences between specified ledgers"};
        return {response, errorStatus};
    }

    for (auto& [k, v] : differences)
    {
        auto diff = response.mutable_ledger_objects()->add_objects();
        auto inBase = v.first;
        auto inDesired = v.second;

        // key does not exist in desired
        if (!inDesired)
        {
            diff->set_key(k.data(), k.size());
        }
        else
        {
            ASSERT(
                inDesired->size() > 0,
                "ripple::doLedgerDiffGrpc : non-empty desired");
            diff->set_key(k.data(), k.size());
            if (request.include_blobs())
            {
                diff->set_data(inDesired->data(), inDesired->size());
            }
        }
    }
    return {response, status};
}

}  // namespace ripple
