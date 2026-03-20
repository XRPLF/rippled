#include <xrpld/overlay/detail/TrafficCount.h>

namespace xrpl {

std::unordered_map<protocol::MessageType, TrafficCount::category> const kTYPE_LOOKUP = {
    {protocol::mtPING, TrafficCount::category::Base},
    {protocol::mtSTATUS_CHANGE, TrafficCount::category::Base},
    {protocol::mtMANIFESTS, TrafficCount::category::Manifests},
    {protocol::mtENDPOINTS, TrafficCount::category::Overlay},
    {protocol::mtTRANSACTION, TrafficCount::category::Transaction},
    {protocol::mtVALIDATOR_LIST, TrafficCount::category::Validatorlist},
    {protocol::mtVALIDATOR_LIST_COLLECTION, TrafficCount::category::Validatorlist},
    {protocol::mtVALIDATION, TrafficCount::category::Validation},
    {protocol::mtPROPOSE_LEDGER, TrafficCount::category::Proposal},
    {protocol::mtPROOF_PATH_REQ, TrafficCount::category::ProofPathRequest},
    {protocol::mtPROOF_PATH_RESPONSE, TrafficCount::category::ProofPathResponse},
    {protocol::mtREPLAY_DELTA_REQ, TrafficCount::category::ReplayDeltaRequest},
    {protocol::mtREPLAY_DELTA_RESPONSE, TrafficCount::category::ReplayDeltaResponse},
    {protocol::mtHAVE_TRANSACTIONS, TrafficCount::category::HaveTransactions},
    {protocol::mtTRANSACTIONS, TrafficCount::category::RequestedTransactions},
    {protocol::mtSQUELCH, TrafficCount::category::Squelch},
};

TrafficCount::category
TrafficCount::categorize(
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    bool inbound)
{
    if (auto item = kTYPE_LOOKUP.find(type); item != kTYPE_LOOKUP.end())
        return item->second;

    if (type == protocol::mtHAVE_SET)
        return inbound ? TrafficCount::category::GetSet : TrafficCount::category::ShareSet;

    if (auto msg = dynamic_cast<protocol::TMLedgerData const*>(&message))
    {
        if (msg->type() == protocol::liTS_CANDIDATE)
        {
            return (inbound && !msg->has_requestcookie()) ? TrafficCount::category::LdTscGet
                                                          : TrafficCount::category::LdTscShare;
        }

        if (msg->type() == protocol::liTX_NODE)
        {
            return (inbound && !msg->has_requestcookie()) ? TrafficCount::category::LdTxnGet
                                                          : TrafficCount::category::LdTxnShare;
        }

        if (msg->type() == protocol::liAS_NODE)
        {
            return (inbound && !msg->has_requestcookie()) ? TrafficCount::category::LdAsnGet
                                                          : TrafficCount::category::LdAsnShare;
        }

        return (inbound && !msg->has_requestcookie()) ? TrafficCount::category::LdGet
                                                      : TrafficCount::category::LdShare;
    }

    if (auto msg = dynamic_cast<protocol::TMGetLedger const*>(&message))
    {
        if (msg->itype() == protocol::liTS_CANDIDATE)
        {
            return (inbound || msg->has_requestcookie()) ? TrafficCount::category::GlTscShare
                                                         : TrafficCount::category::GlTscGet;
        }

        if (msg->itype() == protocol::liTX_NODE)
        {
            return (inbound || msg->has_requestcookie()) ? TrafficCount::category::GlTxnShare
                                                         : TrafficCount::category::GlTxnGet;
        }

        if (msg->itype() == protocol::liAS_NODE)
        {
            return (inbound || msg->has_requestcookie()) ? TrafficCount::category::GlAsnShare
                                                         : TrafficCount::category::GlAsnGet;
        }

        return (inbound || msg->has_requestcookie()) ? TrafficCount::category::GlShare
                                                     : TrafficCount::category::GlGet;
    }

    if (auto msg = dynamic_cast<protocol::TMGetObjectByHash const*>(&message))
    {
        if (msg->type() == protocol::TMGetObjectByHash::otLEDGER)
        {
            return (msg->query() == inbound) ? TrafficCount::category::ShareHashLedger
                                             : TrafficCount::category::GetHashLedger;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTION)
        {
            return (msg->query() == inbound) ? TrafficCount::category::ShareHashTx
                                             : TrafficCount::category::GetHashTx;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTION_NODE)
        {
            return (msg->query() == inbound) ? TrafficCount::category::ShareHashTxnode
                                             : TrafficCount::category::GetHashTxnode;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otSTATE_NODE)
        {
            return (msg->query() == inbound) ? TrafficCount::category::ShareHashAsnode
                                             : TrafficCount::category::GetHashAsnode;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otCAS_OBJECT)
        {
            return (msg->query() == inbound) ? TrafficCount::category::ShareCasObject
                                             : TrafficCount::category::GetCasObject;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otFETCH_PACK)
        {
            return (msg->query() == inbound) ? TrafficCount::category::ShareFetchPack
                                             : TrafficCount::category::GetFetchPack;
        }

        if (msg->type() == protocol::TMGetObjectByHash::otTRANSACTIONS)
            return TrafficCount::category::GetTransactions;

        return (msg->query() == inbound) ? TrafficCount::category::ShareHash
                                         : TrafficCount::category::GetHash;
    }

    return TrafficCount::category::Unknown;
}
}  // namespace xrpl
