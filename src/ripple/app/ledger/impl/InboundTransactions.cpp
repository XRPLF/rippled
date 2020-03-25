//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/ledger/impl/TransactionAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/core/JobQueue.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/resource/Fees.h>
#include <memory>
#include <mutex>

namespace ripple {

enum {
    // Ideal number of peers to start with
    startPeers = 2,

    // How many rounds to keep a set
    setKeepRounds = 3,
};

class InboundTransactionSet
{
    // A transaction set we generated, acquired, or are acquiring
public:
    std::uint32_t mSeq;
    TransactionAcquire::pointer mAcquire;
    std::shared_ptr<SHAMap> mSet;

    InboundTransactionSet(std::uint32_t seq, std::shared_ptr<SHAMap> const& set)
        : mSeq(seq), mSet(set)
    {
        ;
    }
    InboundTransactionSet() : mSeq(0)
    {
        ;
    }
};

class InboundTransactionsImp : public InboundTransactions, public Stoppable
{
public:
    Application& app_;

    InboundTransactionsImp(
        Application& app,
        Stoppable& parent,
        beast::insight::Collector::ptr const& collector,
        std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet)
        : Stoppable("InboundTransactions", parent)
        , app_(app)
        , m_seq(0)
        , m_zeroSet(m_map[uint256()])
        , m_gotSet(std::move(gotSet))
    {
        m_zeroSet.mSet = std::make_shared<SHAMap>(
            SHAMapType::TRANSACTION, uint256(), app_.family());
        m_zeroSet.mSet->setUnbacked();
    }

    TransactionAcquire::pointer
    getAcquire(uint256 const& hash)
    {
        {
            std::lock_guard sl(mLock);

            auto it = m_map.find(hash);

            if (it != m_map.end())
                return it->second.mAcquire;
        }
        return {};
    }

    std::shared_ptr<SHAMap>
    getSet(uint256 const& hash, bool acquire) override
    {
        TransactionAcquire::pointer ta;

        {
            std::lock_guard sl(mLock);

            auto it = m_map.find(hash);

            if (it != m_map.end())
            {
                if (acquire)
                {
                    it->second.mSeq = m_seq;
                    if (it->second.mAcquire)
                    {
                        it->second.mAcquire->stillNeed();
                    }
                }
                return it->second.mSet;
            }

            if (!acquire || isStopping())
                return std::shared_ptr<SHAMap>();

            ta = std::make_shared<TransactionAcquire>(app_, hash);

            auto& obj = m_map[hash];
            obj.mAcquire = ta;
            obj.mSeq = m_seq;
        }

        ta->init(startPeers);

        return {};
    }

    /** We received a TMLedgerData from a peer.
     */
    void
    gotData(
        LedgerHash const& hash,
        std::shared_ptr<Peer> peer,
        std::shared_ptr<protocol::TMLedgerData> packet_ptr) override
    {
        protocol::TMLedgerData& packet = *packet_ptr;

        JLOG(app_.journal("InboundLedger").trace())
            << "Got data (" << packet.nodes().size()
            << ") "
               "for acquiring ledger: "
            << hash;

        TransactionAcquire::pointer ta = getAcquire(hash);

        if (ta == nullptr)
        {
            peer->charge(Resource::feeUnwantedData);
            return;
        }

        std::list<SHAMapNodeID> nodeIDs;
        std::list<Blob> nodeData;
        for (auto const& node : packet.nodes())
        {
            if (!node.has_nodeid() || !node.has_nodedata() ||
                (node.nodeid().size() != 33))
            {
                peer->charge(Resource::feeInvalidRequest);
                return;
            }

            nodeIDs.emplace_back(
                node.nodeid().data(), static_cast<int>(node.nodeid().size()));
            nodeData.emplace_back(
                node.nodedata().begin(), node.nodedata().end());
        }

        if (!ta->takeNodes(nodeIDs, nodeData, peer).isUseful())
            peer->charge(Resource::feeUnwantedData);
    }

    void
    giveSet(
        uint256 const& hash,
        std::shared_ptr<SHAMap> const& set,
        bool fromAcquire) override
    {
        bool isNew = true;

        {
            std::lock_guard sl(mLock);

            auto& inboundSet = m_map[hash];

            if (inboundSet.mSeq < m_seq)
                inboundSet.mSeq = m_seq;

            if (inboundSet.mSet)
                isNew = false;
            else
                inboundSet.mSet = set;

            inboundSet.mAcquire.reset();
        }

        if (isNew)
            m_gotSet(set, fromAcquire);
    }

    Json::Value
    getInfo() override
    {
        Json::Value ret(Json::objectValue);

        Json::Value& sets = (ret["sets"] = Json::arrayValue);

        {
            std::lock_guard sl(mLock);

            ret["seq"] = m_seq;

            for (auto const& it : m_map)
            {
                Json::Value& set = sets[to_string(it.first)];
                set["seq"] = it.second.mSeq;
                if (it.second.mSet)
                    set["state"] = "complete";
                else if (it.second.mAcquire)
                    set["state"] = "acquiring";
                else
                    set["state"] = "dead";
            }
        }

        return ret;
    }

    void
    newRound(std::uint32_t seq) override
    {
        std::lock_guard lock(mLock);

        // Protect zero set from expiration
        m_zeroSet.mSeq = seq;

        if (m_seq != seq)
        {
            m_seq = seq;

            auto it = m_map.begin();

            std::uint32_t const minSeq =
                (seq < setKeepRounds) ? 0 : (seq - setKeepRounds);
            std::uint32_t maxSeq = seq + setKeepRounds;

            while (it != m_map.end())
            {
                if (it->second.mSeq < minSeq || it->second.mSeq > maxSeq)
                    it = m_map.erase(it);
                else
                    ++it;
            }
        }
    }

    void
    onStop() override
    {
        std::lock_guard lock(mLock);

        m_map.clear();

        stopped();
    }

private:
    using MapType = hash_map<uint256, InboundTransactionSet>;

    std::recursive_mutex mLock;

    MapType m_map;
    std::uint32_t m_seq;

    // The empty transaction set whose hash is zero
    InboundTransactionSet& m_zeroSet;

    std::function<void(std::shared_ptr<SHAMap> const&, bool)> m_gotSet;
};

//------------------------------------------------------------------------------

InboundTransactions::~InboundTransactions() = default;

std::unique_ptr<InboundTransactions>
make_InboundTransactions(
    Application& app,
    Stoppable& parent,
    beast::insight::Collector::ptr const& collector,
    std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet)
{
    return std::make_unique<InboundTransactionsImp>(
        app, parent, collector, std::move(gotSet));
}

}  // namespace ripple
