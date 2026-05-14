/** @file
 *  Implementation of `TrafficCount::categorize()` — the single entry point
 *  that maps every overlay protocol message to one of the fine-grained
 *  `TrafficCount::Category` values used for bandwidth telemetry.
 *
 *  Classification uses two stages:
 *  1. An O(1) lookup in `kTYPE_LOOKUP` for message types whose category is
 *     fully determined by the wire type discriminator alone.
 *  2. Three `dynamic_cast` branches for `TMLedgerData`, `TMGetLedger`, and
 *     `TMGetObjectByHash`, whose category depends on protobuf fields
 *     (`requestcookie`, `itype`, `query`) that are invisible to the wire
 *     type alone.
 *
 *  This file is invoked on every inbound message (via `PeerImp`) and once
 *  per outbound `Message` at construction time. Keep this path allocation-free
 *  and branch-minimal.
 */
#include <xrpld/overlay/detail/TrafficCount.h>

#include <google/protobuf/message.h>

#include <xrpl.pb.h>

#include <unordered_map>

namespace xrpl {

/** O(1) lookup table mapping wire message types to traffic categories.
 *
 *  Covers all types whose category is fully determined by the protobuf type
 *  discriminator alone — i.e. no field inspection is required.  Initialized
 *  once at program startup; never mutated at runtime.
 *
 *  Types omitted here require deeper protobuf inspection and are handled by
 *  the `dynamic_cast` branches in `categorize()`:
 *  - `mtHAVE_SET`      — direction-split: `GetSet` vs `ShareSet`
 *  - `mtLEDGER_DATA`   — split by `itype()` and `has_requestcookie()`
 *  - `mtGET_LEDGER`    — split by `itype()` and `has_requestcookie()`
 *  - `mtGET_OBJECTS`   — split by `type()` and `query()`
 */
std::unordered_map<protocol::MessageType, TrafficCount::Category> const kTYPE_LOOKUP = {
    {protocol::mtPING, TrafficCount::Category::Base},
    {protocol::mtSTATUS_CHANGE, TrafficCount::Category::Base},
    {protocol::mtMANIFESTS, TrafficCount::Category::Manifests},
    {protocol::mtENDPOINTS, TrafficCount::Category::Overlay},
    {protocol::mtTRANSACTION, TrafficCount::Category::Transaction},
    {protocol::mtVALIDATOR_LIST, TrafficCount::Category::Validatorlist},
    {protocol::mtVALIDATOR_LIST_COLLECTION, TrafficCount::Category::Validatorlist},
    {protocol::mtVALIDATION, TrafficCount::Category::Validation},
    {protocol::mtPROPOSE_LEDGER, TrafficCount::Category::Proposal},
    {protocol::mtPROOF_PATH_REQ, TrafficCount::Category::ProofPathRequest},
    {protocol::mtPROOF_PATH_RESPONSE, TrafficCount::Category::ProofPathResponse},
    {protocol::mtREPLAY_DELTA_REQ, TrafficCount::Category::ReplayDeltaRequest},
    {protocol::mtREPLAY_DELTA_RESPONSE, TrafficCount::Category::ReplayDeltaResponse},
    {protocol::mtHAVE_TRANSACTIONS, TrafficCount::Category::HaveTransactions},
    {protocol::mtTRANSACTIONS, TrafficCount::Category::RequestedTransactions},
    {protocol::mtSQUELCH, TrafficCount::Category::Squelch},
};

/** Classify a protocol message into the most specific traffic category.
 *
 *  Uses a two-stage strategy:
 *  1. `kTYPE_LOOKUP` handles types whose category is determined by the wire
 *     type discriminator alone (O(1), no allocation).
 *  2. `dynamic_cast` branches inspect protobuf fields for the three complex
 *     types: `TMLedgerData`, `TMGetLedger`, and `TMGetObjectByHash`.
 *
 *  **`TMLedgerData` (`requestcookie` semantics):**
 *  An inbound `TMLedgerData` *without* a `requestcookie` is data the local
 *  node actively fetched → `*_get`.  A `requestcookie` present means the
 *  response was originally requested by a third peer and forwarded through
 *  this node → `*_share`, even though it arrived inbound.
 *
 *  **`TMGetLedger` (reverse `requestcookie` semantics):**
 *  An inbound `TMGetLedger` means a peer is asking for data the local node
 *  holds → `*_share`.  An outbound request *without* a cookie is the local
 *  node actively fetching → `*_get`.  A cookie on an outbound message again
 *  signals a forwarded request → `*_share`.
 *
 *  **`TMGetObjectByHash` (`query()` flag):**
 *  `msg->query() == true` marks a request; `false` marks a response.
 *  The expression `msg->query() == inbound` is `true` when the message is a
 *  request flowing in the expected direction (inbound query = peer requesting
 *  from us = share operation), resolving to `*_share`; otherwise `*_get`.
 *  `otTRANSACTIONS` is always `GetTransactions` regardless of direction.
 *
 *  @param message  The deserialized protobuf message object; used for
 *      `dynamic_cast` on the three complex types.
 *  @param type     The wire-level message type discriminator extracted from
 *      the 6- or 10-byte overlay header before deserialization.
 *  @param inbound  `true` if the message arrived from a remote peer;
 *      `false` if it is being prepared for outbound transmission.
 *  @return The most specific matching `Category`, or `Category::Unknown` if
 *      no branch matches.  The `Unknown` category is always present in
 *      `counts_`, so callers need not special-case it.
 */
TrafficCount::Category
TrafficCount::categorize(
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    bool inbound)
{
    if (auto item = kTYPE_LOOKUP.find(type); item != kTYPE_LOOKUP.end())
        return item->second;

    if (type == protocol::mtHAVE_SET)
        return inbound ? TrafficCount::Category::GetSet : TrafficCount::Category::ShareSet;

    if (auto msg = dynamic_cast<protocol::TMLedgerData const*>(&message))
    {
        if (msg->type() == protocol::liTS_CANDIDATE)
        {
            return (inbound && !msg->has_requestcookie()) ? TrafficCount::Category::LdTscGet
                                                          : TrafficCount::Category::LdTscShare;
        }

        if (msg->type() == protocol::liTX_NODE)
        {
            return (inbound && !msg->has_requestcookie()) ? TrafficCount::Category::LdTxnGet
                                                          : TrafficCount::Category::LdTxnShare;
        }

        if (msg->type() == protocol::liAS_NODE)
        {
            return (inbound && !msg->has_requestcookie()) ? TrafficCount::Category::LdAsnGet
                                                          : TrafficCount::Category::LdAsnShare;
        }

        return (inbound && !msg->has_requestcookie()) ? TrafficCount::Category::LdGet
                                                      : TrafficCount::Category::LdShare;
    }

    if (auto msg = dynamic_cast<protocol::TMGetLedger const*>(&message))
    {
        if (msg->itype() == protocol::liTS_CANDIDATE)
        {
            return (inbound || msg->has_requestcookie()) ? TrafficCount::Category::GlTscShare
                                                         : TrafficCount::Category::GlTscGet;
        }

        if (msg->itype() == protocol::liTX_NODE)
        {
            return (inbound || msg->has_requestcookie()) ? TrafficCount::Category::GlTxnShare
                                                         : TrafficCount::Category::GlTxnGet;
        }

        if (msg->itype() == protocol::liAS_NODE)
        {
            return (inbound || msg->has_requestcookie()) ? TrafficCount::Category::GlAsnShare
                                                         : TrafficCount::Category::GlAsnGet;
        }

        return (inbound || msg->has_requestcookie()) ? TrafficCount::Category::GlShare
                                                     : TrafficCount::Category::GlGet;
    }

    if (auto msg = dynamic_cast<protocol::TMGetObjectByHash const*>(&message))
    {
        if (msg->type() == protocol::TMGetObjectByHash::otLEDGER)
        {
            return (msg->query() == inbound) ? TrafficCount::Category::ShareHashLedger
                                             : TrafficCount::Category::GetHashLedger;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTION)
        {
            return (msg->query() == inbound) ? TrafficCount::Category::ShareHashTx
                                             : TrafficCount::Category::GetHashTx;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTION_NODE)
        {
            return (msg->query() == inbound) ? TrafficCount::Category::ShareHashTxnode
                                             : TrafficCount::Category::GetHashTxnode;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otSTATE_NODE)
        {
            return (msg->query() == inbound) ? TrafficCount::Category::ShareHashAsnode
                                             : TrafficCount::Category::GetHashAsnode;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otCAS_OBJECT)
        {
            return (msg->query() == inbound) ? TrafficCount::Category::ShareCasObject
                                             : TrafficCount::Category::GetCasObject;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otFETCH_PACK)
        {
            return (msg->query() == inbound) ? TrafficCount::Category::ShareFetchPack
                                             : TrafficCount::Category::GetFetchPack;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTIONS)
            return TrafficCount::Category::GetTransactions;

        return (msg->query() == inbound) ? TrafficCount::Category::ShareHash
                                         : TrafficCount::Category::GetHash;
    }

    return TrafficCount::Category::Unknown;
}
}  // namespace xrpl
