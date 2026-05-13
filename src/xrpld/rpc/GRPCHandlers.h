/** @file
 *  Declares the four gRPC ledger handler functions that form XRPL's
 *  Protocol Buffers API surface.
 *
 *  Every handler follows the same contract: it accepts a
 *  `RPC::GRPCContext<RequestType>` (which bundles the protobuf request with
 *  the shared `Context` infrastructure for role checking, load accounting,
 *  and coroutine dispatch) and returns a `(ResponseType, grpc::Status)` pair.
 *  When the status is not `grpc::Status::OK`, the transport layer discards
 *  the response object and forwards only the error status to the client.
 *  Implementations can therefore return a default-constructed response on
 *  any error path without risk of sending partial data.
 *
 *  Role checks, load-shed decisions, and node-condition preconditions are
 *  enforced by `GRPCServer::CallData::process()` before any handler is
 *  invoked, so the handlers themselves may assume the request is already
 *  authorized and the node is in a valid state to serve it.
 */

#pragma once

#include <xrpld/rpc/Context.h>

#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>

#include <grpcpp/grpcpp.h>

namespace xrpl {

/** Return full header information for the requested ledger.
 *
 *  Resolves the ledger identified by the request's specifier (sequence number
 *  or hash), serializes its header fields into the response, and optionally
 *  includes the transaction list and/or state-object diff relative to the
 *  previous ledger. This is the gRPC equivalent of the `ledger` JSON-RPC
 *  command.
 *
 *  @param context  gRPC dispatch envelope carrying the `GetLedgerRequest`
 *      protobuf and the shared application context.
 *  @return A pair of `(GetLedgerResponse, grpc::Status)`. On error the
 *      status is non-OK and the response is discarded by the caller.
 */
std::pair<org::xrpl::rpc::v1::GetLedgerResponse, grpc::Status>
doLedgerGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest>& context);

/** Fetch a single ledger state object by key.
 *
 *  Looks up the serialized ledger entry (SLE) identified by the key in the
 *  request and returns its raw binary blob. Clients use this to retrieve
 *  individual account roots, trust lines, offers, or any other SLE type
 *  without pulling the entire state map.
 *
 *  @param context  gRPC dispatch envelope carrying the `GetLedgerEntryRequest`
 *      protobuf and the shared application context.
 *  @return A pair of `(GetLedgerEntryResponse, grpc::Status)`. Returns
 *      `NOT_FOUND` status when the requested key does not exist in the ledger.
 */
std::pair<org::xrpl::rpc::v1::GetLedgerEntryResponse, grpc::Status>
doLedgerEntryGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest>& context);

/** Return a paginated slice of the ledger's raw state map.
 *
 *  Iterates the SHAMap state trie starting at the opaque `marker` from the
 *  previous page (or from the beginning if absent), returning up to the
 *  requested number of serialized leaf objects. A non-empty `marker` in the
 *  response signals that more data is available. The fixed page size of 2048
 *  objects applies regardless of role. Supports an `end_marker` for
 *  range-bounded parallel replication.
 *
 *  This is the primary mechanism for external services to replicate a full
 *  ledger state one page at a time.
 *
 *  @param context  gRPC dispatch envelope carrying the `GetLedgerDataRequest`
 *      protobuf and the shared application context.
 *  @return A pair of `(GetLedgerDataResponse, grpc::Status)`. On error the
 *      status is non-OK and the response is discarded by the caller.
 */
std::pair<org::xrpl::rpc::v1::GetLedgerDataResponse, grpc::Status>
doLedgerDataGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerDataRequest>& context);

/** Compute the incremental difference between two ledgers' state maps.
 *
 *  Resolves both `base_ledger` and `desired_ledger` from their specifiers,
 *  downcasts each `ReadView` to a fully-validated `Ledger` (returning
 *  `NOT_FOUND` if either is not validated), and calls
 *  `baseLedger->stateMap().compare(...)` to enumerate added, modified, and
 *  deleted state objects. Each changed key is included in the response;
 *  raw object blobs are included only when `include_blobs` is set in the
 *  request.
 *
 *  Purpose-built for light clients and indexers (e.g., Clio) that track
 *  incremental ledger mutations without replaying the full transaction set.
 *
 *  @param context  gRPC dispatch envelope carrying the `GetLedgerDiffRequest`
 *      protobuf and the shared application context.
 *  @return A pair of `(GetLedgerDiffResponse, grpc::Status)`. Returns
 *      `NOT_FOUND` if either ledger cannot be resolved or is not a validated
 *      ledger; returns `RESOURCE_EXHAUSTED` if the difference set overflows
 *      the internal comparison limit.
 */
std::pair<org::xrpl::rpc::v1::GetLedgerDiffResponse, grpc::Status>
doLedgerDiffGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerDiffRequest>& context);

}  // namespace xrpl
