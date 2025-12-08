#include <xrpld/overlay/detail/TrafficCount.h>

namespace ripple {

std::unordered_map<protocol::MessageType, TrafficCount::category> const
    type_lookup = {
        {protocol::mtPING, TrafficCount::category::base},
        {protocol::mtSTATUS_CHANGE, TrafficCount::category::base},
        {protocol::mtMANIFESTS, TrafficCount::category::manifests},
        {protocol::mtENDPOINTS, TrafficCount::category::overlay},
        {protocol::mtTRANSACTION, TrafficCount::category::transaction},
        {protocol::mtVALIDATORLIST, TrafficCount::category::validatorlist},
        {protocol::mtVALIDATORLISTCOLLECTION,
         TrafficCount::category::validatorlist},
        {protocol::mtVALIDATION, TrafficCount::category::validation},
        {protocol::mtPROPOSE_LEDGER, TrafficCount::category::proposal},
        {protocol::mtPROOF_PATH_REQ,
         TrafficCount::category::proof_path_request},
        {protocol::mtPROOF_PATH_RESPONSE,
         TrafficCount::category::proof_path_response},
        {protocol::mtREPLAY_DELTA_REQ,
         TrafficCount::category::replay_delta_request},
        {protocol::mtREPLAY_DELTA_RESPONSE,
         TrafficCount::category::replay_delta_response},
        {protocol::mtHAVE_TRANSACTIONS,
         TrafficCount::category::have_transactions},
        {protocol::mtTRANSACTIONS,
         TrafficCount::category::requested_transactions},
        {protocol::mtSQUELCH, TrafficCount::category::squelch},
};

TrafficCount::category
TrafficCount::categorize(
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    bool inbound)
{
    if (auto item = type_lookup.find(type); item != type_lookup.end())
        return item->second;

    if (type == protocol::mtHAVE_SET)
        return inbound ? TrafficCount::category::get_set
                       : TrafficCount::category::share_set;

    if (auto msg = dynamic_cast<protocol::TMLedgerData const*>(&message))
    {
        if (msg->type() == protocol::liTS_CANDIDATE)
            return (inbound && !msg->has_requestcookie())
                ? TrafficCount::category::ld_tsc_get
                : TrafficCount::category::ld_tsc_share;

        if (msg->type() == protocol::liTX_NODE)
            return (inbound && !msg->has_requestcookie())
                ? TrafficCount::category::ld_txn_get
                : TrafficCount::category::ld_txn_share;

        if (msg->type() == protocol::liAS_NODE)
            return (inbound && !msg->has_requestcookie())
                ? TrafficCount::category::ld_asn_get
                : TrafficCount::category::ld_asn_share;

        return (inbound && !msg->has_requestcookie())
            ? TrafficCount::category::ld_get
            : TrafficCount::category::ld_share;
    }

    if (auto msg = dynamic_cast<protocol::TMGetLedger const*>(&message))
    {
        if (msg->itype() == protocol::liTS_CANDIDATE)
            return (inbound || msg->has_requestcookie())
                ? TrafficCount::category::gl_tsc_share
                : TrafficCount::category::gl_tsc_get;

        if (msg->itype() == protocol::liTX_NODE)
            return (inbound || msg->has_requestcookie())
                ? TrafficCount::category::gl_txn_share
                : TrafficCount::category::gl_txn_get;

        if (msg->itype() == protocol::liAS_NODE)
            return (inbound || msg->has_requestcookie())
                ? TrafficCount::category::gl_asn_share
                : TrafficCount::category::gl_asn_get;

        return (inbound || msg->has_requestcookie())
            ? TrafficCount::category::gl_share
            : TrafficCount::category::gl_get;
    }

    if (auto msg = dynamic_cast<protocol::TMGetObjectByHash const*>(&message))
    {
        if (msg->type() == protocol::TMGetObjectByHash::otLEDGER)
            return (msg->query() == inbound)
                ? TrafficCount::category::share_hash_ledger
                : TrafficCount::category::get_hash_ledger;

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTION)
            return (msg->query() == inbound)
                ? TrafficCount::category::share_hash_tx
                : TrafficCount::category::get_hash_tx;

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTION_NODE)
            return (msg->query() == inbound)
                ? TrafficCount::category::share_hash_txnode
                : TrafficCount::category::get_hash_txnode;

        if (msg->type() == protocol::TMGetObjectByHash::otSTATE_NODE)
            return (msg->query() == inbound)
                ? TrafficCount::category::share_hash_asnode
                : TrafficCount::category::get_hash_asnode;

        if (msg->type() == protocol::TMGetObjectByHash::otCAS_OBJECT)
            return (msg->query() == inbound)
                ? TrafficCount::category::share_cas_object
                : TrafficCount::category::get_cas_object;

        if (msg->type() == protocol::TMGetObjectByHash::otFETCH_PACK)
            return (msg->query() == inbound)
                ? TrafficCount::category::share_fetch_pack
                : TrafficCount::category::get_fetch_pack;

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTIONS)
            return TrafficCount::category::get_transactions;

        return (msg->query() == inbound) ? TrafficCount::category::share_hash
                                         : TrafficCount::category::get_hash;
    }

    return TrafficCount::category::unknown;
}
}  // namespace ripple
