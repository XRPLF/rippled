#ifndef XRPL_RPC_GRPCHANDLER_H_INCLUDED
#define XRPL_RPC_GRPCHANDLER_H_INCLUDED

#include <xrpld/rpc/Context.h>

#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>

#include <grpcpp/grpcpp.h>

namespace ripple {

/*
 * These handlers are for gRPC. They each take in a protobuf message that is
 * nested inside RPC::GRPCContext<T>, where T is the request type
 * The return value is the response type, as well as a status
 * If the status is not Status::OK (meaning an error occurred), then only
 * the status will be sent to the client, and the response will be ommitted
 */

std::pair<org::xrpl::rpc::v1::GetLedgerResponse, grpc::Status>
doLedgerGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest>& context);

std::pair<org::xrpl::rpc::v1::GetLedgerEntryResponse, grpc::Status>
doLedgerEntryGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest>& context);

std::pair<org::xrpl::rpc::v1::GetLedgerDataResponse, grpc::Status>
doLedgerDataGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerDataRequest>& context);

std::pair<org::xrpl::rpc::v1::GetLedgerDiffResponse, grpc::Status>
doLedgerDiffGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerDiffRequest>& context);

}  // namespace ripple

#endif
