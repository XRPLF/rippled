/** @file
 *  Implements the `GetLedgerDiff` gRPC handler (`doLedgerDiffGrpc`).
 *
 *  Computes the incremental SHAMap delta between two validated ledgers,
 *  enabling light clients and indexers to track ledger state mutations
 *  without replaying the full transaction set. See `GRPCHandlers.h` for
 *  the full public contract.
 */

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/shamap/SHAMap.h>

#include <grpcpp/support/status.h>
#include <org/xrpl/rpc/v1/get_ledger_diff.pb.h>

#include <limits>
#include <memory>
#include <utility>

namespace xrpl {
std::pair<org::xrpl::rpc::v1::GetLedgerDiffResponse, grpc::Status>
doLedgerDiffGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerDiffRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerDiffRequest const& request = context.params;
    org::xrpl::rpc::v1::GetLedgerDiffResponse response;
    grpc::Status const status = grpc::Status::OK;

    std::shared_ptr<ReadView const> baseLedgerRv;
    std::shared_ptr<ReadView const> desiredLedgerRv;

    if (RPC::ledgerFromSpecifier(baseLedgerRv, request.base_ledger(), context))
    {
        grpc::Status const errorStatus{grpc::StatusCode::NOT_FOUND, "base ledger not found"};
        return {response, errorStatus};
    }

    if (RPC::ledgerFromSpecifier(desiredLedgerRv, request.desired_ledger(), context))
    {
        grpc::Status const errorStatus{grpc::StatusCode::NOT_FOUND, "desired ledger not found"};
        return {response, errorStatus};
    }

    // `ledgerFromSpecifier` can return a ReadView that is not a finalized
    // Ledger (e.g., the currently-building open ledger). Only a concrete
    // `Ledger` exposes `stateMap()`, so the downcast acts as a validation
    // gate: a null result means the specifier resolved to an unvalidated view.
    std::shared_ptr<Ledger const> const baseLedger =
        std::dynamic_pointer_cast<Ledger const>(baseLedgerRv);
    if (!baseLedger)
    {
        grpc::Status const errorStatus{grpc::StatusCode::NOT_FOUND, "base ledger not validated"};
        return {response, errorStatus};
    }

    std::shared_ptr<Ledger const> const desiredLedger =
        std::dynamic_pointer_cast<Ledger const>(desiredLedgerRv);
    if (!desiredLedger)
    {
        grpc::Status const errorStatus{grpc::StatusCode::NOT_FOUND, "desired ledger not validated"};
        return {response, errorStatus};
    }

    SHAMap::Delta differences;

    // No practical cap: INT_MAX allows arbitrarily large diffs between
    // distant ledgers. RESOURCE_EXHAUSTED is returned only if compare()
    // somehow exhausts this limit, which guards against pathological
    // SHAMap divergence bugs rather than normal operation.
    int const maxDifferences = std::numeric_limits<int>::max();

    bool const res =
        baseLedger->stateMap().compare(desiredLedger->stateMap(), differences, maxDifferences);
    if (!res)
    {
        grpc::Status const errorStatus{
            grpc::StatusCode::RESOURCE_EXHAUSTED, "too many differences between specified ledgers"};
        return {response, errorStatus};
    }

    // Each SHAMap::Delta entry is a (base item, desired item) pair.
    // A null desired item means the key was deleted; a null base item means
    // it was added; both non-null means the serialized object changed.
    // Only the desired-side blob is ever emitted — callers that need the
    // old value of a modified key must fetch it separately.
    for (auto& [k, v] : differences)
    {
        auto diff = response.mutable_ledger_objects()->add_objects();
        auto inBase = v.first;
        auto inDesired = v.second;

        if (!inDesired)
        {
            // Deleted key: emit the key with no data blob.
            diff->set_key(k.data(), k.size());
        }
        else
        {
            XRPL_ASSERT(inDesired->size() > 0, "xrpl::doLedgerDiffGrpc : non-empty desired");
            diff->set_key(k.data(), k.size());
            if (request.include_blobs())
            {
                diff->set_data(inDesired->data(), inDesired->size());
            }
        }
    }
    return {response, status};
}

}  // namespace xrpl
