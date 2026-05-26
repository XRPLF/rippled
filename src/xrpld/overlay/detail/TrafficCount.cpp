#include <xrpld/overlay/detail/TrafficCount.h>

#include <google/protobuf/message.h>

#include <xrpl.pb.h>

#include <unordered_map>

namespace xrpl {

std::unordered_map<protocol::MessageType, TrafficCount::Category> const kTypeLookup = {
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

TrafficCount::Category
TrafficCount::categorize(
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    bool inbound)
{
    if (auto item = kTypeLookup.find(type); item != kTypeLookup.end())
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
