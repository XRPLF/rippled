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

#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/ledger/impl/TransactionAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/resource/Fees.h>
#include <cassert>
#include <memory>
#include <mutex>

namespace ripple {

struct InboundTransactionSet
{
    // A transaction set we generated, acquired, or are acquiring
    std::shared_ptr<TransactionAcquire> acquire;
    std::shared_ptr<SHAMap> txset;
    std::uint32_t seq = 0;

    InboundTransactionSet() = default;
    InboundTransactionSet(InboundTransactionSet&& other) = default;

    InboundTransactionSet&
    operator=(InboundTransactionSet&& other) = delete;

    InboundTransactionSet(InboundTransactionSet const& other) = delete;
    InboundTransactionSet&
    operator=(InboundTransactionSet const& other) = delete;
};

class InboundTransactionsImp : public InboundTransactions
{
    /** The initial number of peers to query when fetching a transaction set. */
    static constexpr int const startPeers = 2;

    /** How many rounds to keep an inbound transaction set for */
    static constexpr std::uint32_t const setKeepRounds = 3;

public:
    InboundTransactionsImp(
        Application& app,
        beast::insight::Collector::ptr const& collector,
        std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet,
        std::unique_ptr<PeerSetBuilder> peerSetBuilder)
        : app_(app)
        , m_gotSet(std::move(gotSet))
        , peerSetBuilder_(std::move(peerSetBuilder))
    {
        emptyMap_ = std::make_shared<SHAMap>(
            SHAMapType::TRANSACTION, uint256(), app_.getNodeFamily());
        emptyMap_->setUnbacked();
    }

    std::shared_ptr<SHAMap>
    getSet(uint256 const& hash, bool acquire) override
    {
        if (hash.isZero())
            return emptyMap_;

        std::shared_ptr<TransactionAcquire> ta;

        {
            std::lock_guard sl(lock_);

            if (stopping_)
                return {};

            if (auto it = m_map.find(hash); it != m_map.end())
            {
                if (acquire)
                {
                    it->second.seq = m_seq;

                    if (it->second.acquire)
                        it->second.acquire->stillNeed();
                }

                return it->second.txset;
            }

            if (acquire)
            {
                ta = std::make_shared<TransactionAcquire>(
                    app_, hash, peerSetBuilder_->build());

                auto& obj = m_map[hash];
                obj.acquire = ta;
                obj.seq = m_seq;
            }
        }

        if (ta)
            ta->init(startPeers);

        return {};
    }

    /** We received data from a peer. */
    void
    gotData(
        uint256 const& hash,
        std::shared_ptr<Peer> peer,
        std::vector<std::pair<SHAMapNodeID, Slice>> const& data) override
    {
        assert(!data.empty());

        if (hash.isZero())
            return;

        auto ta = [this, &hash]() -> std::shared_ptr<TransactionAcquire> {
            std::lock_guard sl(lock_);
            if (auto it = m_map.find(hash); it != m_map.end())
                return it->second.acquire;
            return {};
        }();

        if (!ta || !ta->takeNodes(data, peer).isUseful())
            peer->charge(Resource::feeUnwantedData);
    }

    void
    giveSet(
        uint256 const& hash,
        std::shared_ptr<SHAMap> const& set,
        bool fromAcquire) override
    {
        if (hash.isZero())
            return;

        bool isNew = true;

        {
            std::lock_guard sl(lock_);

            if (stopping_)
                return;

            auto& inboundSet = m_map[hash];

            if (inboundSet.seq < m_seq)
                inboundSet.seq = m_seq;

            if (inboundSet.txset)
                isNew = false;
            else
                inboundSet.txset = set;

            inboundSet.acquire.reset();
        }

        if (isNew)
            m_gotSet(set, fromAcquire);
    }

    void
    newRound(std::uint32_t seq) override
    {
        assert(
            seq <= std::numeric_limits<std::uint32_t>::max() - setKeepRounds);

        std::lock_guard lock(lock_);

        if (!stopping_ && m_seq != seq)
        {
            m_seq = seq;

            auto it = m_map.begin();

            std::uint32_t const maxSeq = seq + setKeepRounds;
            std::uint32_t const minSeq = seq - std::min(seq, setKeepRounds);

            while (it != m_map.end())
            {
                if (it->second.seq < minSeq || it->second.seq > maxSeq)
                    it = m_map.erase(it);
                else
                    ++it;
            }
        }
    }

    void
    stop() override
    {
        std::lock_guard lock(lock_);
        stopping_ = true;
        m_map.clear();
    }

private:
    Application& app_;

    std::mutex lock_;

    hash_map<uint256, InboundTransactionSet> m_map;

    // The empty transaction set (whose hash is zero)
    std::shared_ptr<SHAMap> emptyMap_;

    std::function<void(std::shared_ptr<SHAMap> const&, bool)> m_gotSet;

    std::unique_ptr<PeerSetBuilder> peerSetBuilder_;

    std::uint32_t m_seq = 0;

    bool stopping_ = false;
};

//------------------------------------------------------------------------------

std::unique_ptr<InboundTransactions>
make_InboundTransactions(
    Application& app,
    beast::insight::Collector::ptr const& collector,
    std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet)
{
    return std::make_unique<InboundTransactionsImp>(
        app, collector, std::move(gotSet), make_PeerSetBuilder(app));
}

}  // namespace ripple
