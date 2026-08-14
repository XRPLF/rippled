#pragma once

#include <xrpld/app/ledger/AcceptedLedger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/detail/AccountTxPaging.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/ReaderPreferringSharedMutex.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/rdb/RelationalDatabase.h>

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

namespace xrpl {

class RWDBDatabase : public RelationalDatabase
{
private:
    struct LedgerData
    {
        LedgerHeader info;
        // Hash lookup for erase/delete. Iteration order is txOrder.
        std::map<uint256, AccountTx> transactions;
        // Transaction IDs in AcceptedLedger txnSeq order (ascending).
        std::vector<uint256> txOrder;
        bool transactionsPurged{false};
    };

    struct AccountTxData
    {
        std::map<uint32_t, std::vector<AccountTx>>
            ledgerTxMap;  // ledgerSeq -> vector of transactions
    };

    Application& app_;
    bool const useTxTables_;

    // SQLite's transactionFromSQL builds a new Transaction per query.
    // Hand out the same copies so TransactionMaster::setStatus cannot
    // rewrite ledgerIndex_ on the store-owned object (or leave
    // eraseStaleTransactionUnlocked pointing at the wrong sequence).
    AccountTx
    detachAccountTx(AccountTx const& src) const
    {
        if (!src.first)
            return src;
        std::string reason;
        auto txn = std::make_shared<Transaction>(src.first->getSTransaction(), reason, app_);
        txn->setStatus(src.first->getStatus());
        txn->setLedger(src.first->getLedger());
        return {std::move(txn), src.second};
    }

    // Drop the hash->seq mapping only when it still names this sequence.
    // The same header can be stored under two sequences (history-fetch /
    // later validation). Pruning the older one must not hide the newer.
    void
    eraseHashMappingIfOwnedUnlocked(uint256 const& hash, LedgerIndex seq)
    {
        auto it = ledgerHashToSeq_.find(hash);
        if (it == ledgerHashToSeq_.end() || it->second != seq)
            return;
        ledgerHashToSeq_.erase(it);
        // Re-point at a surviving row with the same hash (newest first)
        // so a later overwrite of this sequence does not hide an older copy.
        for (auto rit = ledgers_.rbegin(); rit != ledgers_.rend(); ++rit)
        {
            if (rit->first != seq && rit->second.info.hash == hash)
            {
                ledgerHashToSeq_[hash] = rit->first;
                break;
            }
        }
    }

    // Drop any previous row for this sequence so a second save (validation
    // plus fetchForHistory, or a HashRouter SAVED miss) is idempotent.
    void
    replaceLedgerSeqUnlocked(LedgerIndex seq)
    {
        auto existing = ledgers_.find(seq);
        if (existing == ledgers_.end())
            return;

        eraseHashMappingIfOwnedUnlocked(existing->second.info.hash, seq);
        for (auto const& [txHash, _] : existing->second.transactions)
            transactionMap_.erase(txHash);
        for (auto accountIt = accountTxMap_.begin(); accountIt != accountTxMap_.end();)
        {
            accountIt->second.ledgerTxMap.erase(seq);
            if (accountIt->second.ledgerTxMap.empty())
                accountIt = accountTxMap_.erase(accountIt);
            else
                ++accountIt;
        }
    }

    // SQLite deletes AccountTransactions by TransID (all sequences) and
    // INSERT OR REPLACEs the transaction row. If this id is already stored
    // under a different ledger, drop those stale index rows first.
    void
    eraseStaleTransactionUnlocked(uint256 const& id)
    {
        auto it = transactionMap_.find(id);
        if (it == transactionMap_.end())
            return;

        auto const& oldTx = it->second;
        auto const oldSeq = oldTx.first ? oldTx.first->getLedger() : LedgerIndex{0};

        if (oldSeq != 0)
        {
            if (auto ledIt = ledgers_.find(oldSeq); ledIt != ledgers_.end())
            {
                ledIt->second.transactions.erase(id);
                std::erase(ledIt->second.txOrder, id);
            }

            if (oldTx.second)
            {
                for (auto const& account : oldTx.second->getAffectedAccounts())
                {
                    auto accountIt = accountTxMap_.find(account);
                    if (accountIt == accountTxMap_.end())
                        continue;

                    auto seqIt = accountIt->second.ledgerTxMap.find(oldSeq);
                    if (seqIt == accountIt->second.ledgerTxMap.end())
                        continue;

                    std::erase_if(seqIt->second, [&](AccountTx const& tx) {
                        return tx.first && tx.first->getID() == id;
                    });
                    if (seqIt->second.empty())
                        accountIt->second.ledgerTxMap.erase(seqIt);
                    if (accountIt->second.ledgerTxMap.empty())
                        accountTxMap_.erase(accountIt);
                }
            }
        }

        transactionMap_.erase(it);
    }

    // Match SQLite transactionsSQL: bUnlimited means "do not clamp to the
    // page length"; the requested limit still applies. limit 0 / UINT32_MAX
    // fall back to the page length so a privileged caller cannot pull every
    // stored transaction in one response.
    static std::uint32_t
    accountTxResultLimit(AccountTxOptions const& options, bool binary)
    {
        static constexpr std::uint32_t kNonbinaryPageLength = 200;
        static constexpr std::uint32_t kBinaryPageLength = 500;
        auto const pageLength = binary ? kBinaryPageLength : kNonbinaryPageLength;
        if (options.limit == 0 || options.limit == std::numeric_limits<std::uint32_t>::max())
            return pageLength;
        if (!options.bUnlimited)
            return std::min(pageLength, options.limit);
        return options.limit;
    }

    // A min/max of 0 is unbounded, matching AccountTxOptions.
    // clampMarkerToRange only intersects one edge, so a client marker
    // past the other edge inverts [min, max]. Walking that increments
    // past end().
    static auto
    ledgerTxBounds(
        std::map<uint32_t, std::vector<AccountTx>> const& ledgerTxMap,
        std::uint32_t minSeq,
        std::uint32_t maxSeq)
    {
        if (minSeq != 0 && maxSeq != 0 && minSeq > maxSeq)
            return std::pair{ledgerTxMap.end(), ledgerTxMap.end()};

        auto const first = (minSeq == 0) ? ledgerTxMap.begin() : ledgerTxMap.lower_bound(minSeq);
        auto const last = (maxSeq == 0) ? ledgerTxMap.end() : ledgerTxMap.upper_bound(maxSeq);
        return std::pair{first, last};
    }

    // Intersect a marker ledger with a range bound. 0 on the range side
    // means unbounded; takeMax is forward (lower bound), otherwise reverse.
    static std::uint32_t
    clampMarkerToRange(std::uint32_t markerLedger, std::uint32_t rangeBound, bool takeMax)
    {
        if (markerLedger == 0)
            return rangeBound;
        if (rangeBound == 0)
            return markerLedger;
        return takeMax ? std::max(markerLedger, rangeBound) : std::min(markerLedger, rangeBound);
    }

    // Reader-preferring so Linux matches macOS and RPC readers are not
    // starved by saveValidatedLedger / deleteBeforeLedgerSeq writers.
    mutable ReaderPreferringSharedMutex mutex_;

    std::map<LedgerIndex, LedgerData> ledgers_;
    std::map<uint256, LedgerIndex> ledgerHashToSeq_;
    std::map<uint256, AccountTx> transactionMap_;
    std::map<AccountID, AccountTxData> accountTxMap_;

public:
    RWDBDatabase(ServiceRegistry& registry, Config const& config, JobQueue&)
        : app_(registry.getApp()), useTxTables_(config.useTxTables())
    {
    }

    std::optional<LedgerIndex>
    getMinLedgerSeq() override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.begin()->first;
    }

    std::optional<LedgerIndex>
    getTransactionsMinLedgerSeq() override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        for (auto const& [ledgerSeq, ledgerData] : ledgers_)
        {
            if (!ledgerData.transactions.empty())
                return ledgerSeq;
        }
        return std::nullopt;
    }

    std::optional<LedgerIndex>
    getAccountTransactionsMinLedgerSeq() override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        if (accountTxMap_.empty())
            return std::nullopt;
        LedgerIndex minSeq = std::numeric_limits<LedgerIndex>::max();
        for (auto const& [_, accountData] : accountTxMap_)
        {
            if (!accountData.ledgerTxMap.empty())
                minSeq = std::min(minSeq, accountData.ledgerTxMap.begin()->first);
        }
        return minSeq == std::numeric_limits<LedgerIndex>::max()
            ? std::nullopt
            : std::optional<LedgerIndex>(minSeq);
    }

    std::optional<LedgerIndex>
    getMaxLedgerSeq() override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.rbegin()->first;
    }

    void
    deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) override
    {
        if (!useTxTables_)
            return;

        std::unique_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgers_.find(ledgerSeq);
        if (it == ledgers_.end())
            return;

        for (auto const& [txHash, _] : it->second.transactions)
            transactionMap_.erase(txHash);
        it->second.transactions.clear();
        it->second.txOrder.clear();
        it->second.transactionsPurged = true;

        // Keep account_tx indexes in agreement with getTransactionCount().
        for (auto accountIt = accountTxMap_.begin(); accountIt != accountTxMap_.end();)
        {
            accountIt->second.ledgerTxMap.erase(ledgerSeq);
            if (accountIt->second.ledgerTxMap.empty())
                accountIt = accountTxMap_.erase(accountIt);
            else
                ++accountIt;
        }
    }

    void
    deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        std::unique_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgers_.begin();
        while (it != ledgers_.end() && it->first < ledgerSeq)
        {
            if (useTxTables_)
            {
                // Purge per-ledger transaction index before removing the ledger.
                for (auto const& [txHash, _] : it->second.transactions)
                    transactionMap_.erase(txHash);
            }
            eraseHashMappingIfOwnedUnlocked(it->second.info.hash, it->first);
            it = ledgers_.erase(it);
        }

        if (!useTxTables_)
            return;

        // Drop old account transaction buckets and erase empty account entries.
        for (auto accountIt = accountTxMap_.begin(); accountIt != accountTxMap_.end();)
        {
            auto& ledgerTxMap = accountIt->second.ledgerTxMap;
            auto txIt = ledgerTxMap.begin();
            while (txIt != ledgerTxMap.end() && txIt->first < ledgerSeq)
                txIt = ledgerTxMap.erase(txIt);

            if (ledgerTxMap.empty())
            {
                accountIt = accountTxMap_.erase(accountIt);
            }
            else
            {
                ++accountIt;
            }
        }
    }

    void
    deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        if (!useTxTables_)
            return;

        std::unique_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgers_.begin();
        while (it != ledgers_.end() && it->first < ledgerSeq)
        {
            for (auto const& [txHash, _] : it->second.transactions)
            {
                transactionMap_.erase(txHash);
            }
            it->second.transactions.clear();
            it->second.txOrder.clear();
            it->second.transactionsPurged = true;
            ++it;
        }
    }

    void
    deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        if (!useTxTables_)
            return;

        std::unique_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        for (auto accountIt = accountTxMap_.begin(); accountIt != accountTxMap_.end();)
        {
            auto& accountData = accountIt->second;
            auto txIt = accountData.ledgerTxMap.begin();
            while (txIt != accountData.ledgerTxMap.end() && txIt->first < ledgerSeq)
            {
                txIt = accountData.ledgerTxMap.erase(txIt);
            }

            if (accountData.ledgerTxMap.empty())
            {
                accountIt = accountTxMap_.erase(accountIt);
            }
            else
            {
                ++accountIt;
            }
        }
    }

    std::size_t
    getTransactionCount() override
    {
        if (!useTxTables_)
            return 0;

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        return transactionMap_.size();
    }

    std::size_t
    getAccountTransactionCount() override
    {
        if (!useTxTables_)
            return 0;

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        std::size_t count = 0;
        for (auto const& [_, accountData] : accountTxMap_)
        {
            for (auto const& [_, txVector] : accountData.ledgerTxMap)
            {
                count += txVector.size();
            }
        }
        return count;
    }

    CountMinMax
    getLedgerCountMinMax() override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        if (ledgers_.empty())
            return {.numberOfRows = 0, .minLedgerSequence = 0, .maxLedgerSequence = 0};
        return {
            .numberOfRows = ledgers_.size(),
            .minLedgerSequence = ledgers_.begin()->first,
            .maxLedgerSequence = ledgers_.rbegin()->first};
    }

    bool
    saveValidatedLedger(std::shared_ptr<Ledger const> const& ledger, bool current) override
    {
        LedgerData ledgerData;
        ledgerData.info = ledger->header();
        ledgerData.transactionsPurged = false;
        auto j = app_.getJournal("Ledger");
        auto seq = ledger->header().seq;

        JLOG(j.trace()) << "saveValidatedLedger " << (current ? "" : "fromAcquire ") << seq;

        if (!ledger->header().accountHash.isNonZero())
        {
            JLOG(j.fatal()) << "AH is zero: " << getJson({*ledger, {}}).asString();
            UNREACHABLE("xrpl::RWDBDatabase::saveValidatedLedger : account hash is zero");
        }

        if (ledger->header().accountHash != ledger->stateMap().getHash().asUInt256())
        {
            JLOG(j.fatal()) << "sAL: " << ledger->header().accountHash
                            << " != " << ledger->stateMap().getHash();
            JLOG(j.fatal()) << "saveAcceptedLedger: seq=" << seq << ", current=" << current;
            UNREACHABLE("xrpl::RWDBDatabase::saveValidatedLedger : account hash mismatch");
        }

        XRPL_ASSERT(
            ledger->header().txHash == ledger->txMap().getHash().asUInt256(),
            "xrpl::RWDBDatabase::saveValidatedLedger : tx hash mismatch");

        {
            Serializer s(128);
            s.add32(HashPrefix::LedgerMaster);
            addRaw(ledger->header(), s);
            // Persist the header only when the node store is durable.
            // A type=rwdb node store is a NullBackend and discards this.
            app_.getNodeStore().store(
                NodeObjectType::Ledger, std::move(s.modData()), ledger->header().hash, seq);
        }

        std::shared_ptr<AcceptedLedger> aLedger;
        try
        {
            aLedger = app_.getAcceptedLedgerCache().fetch(ledger->header().hash);
            if (!aLedger)
            {
                aLedger = std::make_shared<AcceptedLedger>(ledger);
                app_.getAcceptedLedgerCache().canonicalizeReplaceClient(
                    ledger->header().hash, aLedger);
            }
        }
        catch (std::exception const&)
        {
            JLOG(j.warn()) << "An accepted ledger was missing nodes";
            app_.getLedgerMaster().failedSave(seq, ledger->header().hash);
            app_.getPendingSaves().finishWork(seq);
            return false;
        }

        if (useTxTables_)
        {
            struct TxInsert
            {
                uint256 id;
                AccountTx accTx;
                std::vector<AccountID> affected;
                std::uint32_t txnSeq;
            };

            std::vector<TxInsert> txInserts;

            for (auto const& acceptedLedgerTx : *aLedger)
            {
                auto const& txn = acceptedLedgerTx->getTxn();
                auto const& meta = acceptedLedgerTx->getMeta();
                auto const& id = txn->getTransactionID();
                auto const affectedAccounts = meta.getAffectedAccounts();
                std::string reason;

                auto accTx = std::make_pair(
                    std::make_shared<Transaction>(txn, reason, app_),
                    std::make_shared<TxMeta>(meta));

                // Initialize once at insert time to avoid mutating shared
                // Transaction instances from concurrent read paths.
                accTx.first->setStatus(TransStatus::COMMITTED);
                accTx.first->setLedger(seq);

                txInserts.push_back(
                    TxInsert{
                        .id = id,
                        .accTx = accTx,
                        .affected = std::vector<AccountID>(
                            affectedAccounts.begin(), affectedAccounts.end()),
                        .txnSeq = acceptedLedgerTx->getTxnSeq()});
            }

            {
                std::unique_lock<ReaderPreferringSharedMutex> const lock(mutex_);
                replaceLedgerSeqUnlocked(seq);
                for (auto const& insert : txInserts)
                {
                    eraseStaleTransactionUnlocked(insert.id);
                    ledgerData.transactions.emplace(insert.id, insert.accTx);
                    ledgerData.txOrder.push_back(insert.id);
                    transactionMap_.insert_or_assign(insert.id, insert.accTx);

                    for (auto const& account : insert.affected)
                    {
                        if (!accountTxMap_.contains(account))
                            accountTxMap_[account] = AccountTxData();

                        auto& accountData = accountTxMap_[account];
                        accountData.ledgerTxMap[seq].push_back(insert.accTx);
                    }
                }

                ledgers_[seq] = std::move(ledgerData);
                ledgerHashToSeq_[ledger->header().hash] = seq;
            }

            for (auto const& insert : txInserts)
            {
                app_.getMasterTransaction().inLedger(
                    insert.id, seq, insert.txnSeq, app_.getNetworkIDService().getNetworkID());
            }
            return true;
        }

        {
            std::unique_lock<ReaderPreferringSharedMutex> const lock(mutex_);
            replaceLedgerSeqUnlocked(seq);
            ledgers_[seq] = std::move(ledgerData);
            ledgerHashToSeq_[ledger->header().hash] = seq;
        }
        return true;
    }

    std::optional<LedgerHeader>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgers_.find(ledgerSeq);
        if (it != ledgers_.end())
            return it->second.info;
        return std::nullopt;
    }

    std::optional<LedgerHeader>
    getNewestLedgerInfo() override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.rbegin()->second.info;
    }

    std::optional<LedgerHeader>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgers_.lower_bound(ledgerFirstIndex);
        if (it != ledgers_.end())
            return it->second.info;
        return std::nullopt;
    }

    std::optional<LedgerHeader>
    getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgers_.lower_bound(ledgerFirstIndex);
        if (it == ledgers_.end())
            return std::nullopt;
        return ledgers_.rbegin()->second.info;
    }

    std::optional<LedgerHeader>
    getLedgerInfoByHash(uint256 const& ledgerHash) override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgerHashToSeq_.find(ledgerHash);
        if (it != ledgerHashToSeq_.end())
            return ledgers_.at(it->second).info;
        return std::nullopt;
    }

    uint256
    getHashByIndex(LedgerIndex ledgerIndex) override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgers_.find(ledgerIndex);
        if (it != ledgers_.end())
            return it->second.info.hash;
        return uint256();
    }

    std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = ledgers_.find(ledgerIndex);
        if (it != ledgers_.end())
        {
            return LedgerHashPair{
                .ledgerHash = it->second.info.hash, .parentHash = it->second.info.parentHash};
        }
        return std::nullopt;
    }

    std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        std::map<LedgerIndex, LedgerHashPair> result;
        auto it = ledgers_.lower_bound(minSeq);
        auto end = ledgers_.upper_bound(maxSeq);
        for (; it != end; ++it)
        {
            result[it->first] = LedgerHashPair{
                .ledgerHash = it->second.info.hash, .parentHash = it->second.info.parentHash};
        }
        return result;
    }

    std::variant<AccountTx, TxSearched>
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<std::uint32_t>> const& range,
        ErrorCodeI& ec) override
    {
        if (!useTxTables_)
            return TxSearched::Unknown;

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = transactionMap_.find(id);
        if (it != transactionMap_.end())
        {
            return detachAccountTx(it->second);
        }

        if (range)
        {
            // Inverted ranges (first > last) can appear via CTID + ledgerRange
            // when ledgerSeq >> txnIndex. Do not wrap uint32_t and spin.
            if (range->first() > range->last())
                return TxSearched::Some;

            auto const first = range->first();
            auto const last = range->last();
            auto it = ledgers_.lower_bound(first);
            auto const end = ledgers_.upper_bound(last);
            std::size_t present = 0;
            for (; it != end; ++it)
            {
                if (!it->second.transactionsPurged)
                    ++present;
            }
            auto const expected = static_cast<std::size_t>(last - first) + 1;
            return (present == expected) ? TxSearched::All : TxSearched::Some;
        }

        return TxSearched::Unknown;
    }

    // Approximate rb-tree node overhead for server_info-grade reporting.
    static constexpr size_t kMapNodeOverhead = 40;

private:
    std::uint64_t
    getBytesUsedLedgerUnlocked() const
    {
        std::uint64_t size = 0;

        size += ledgers_.size() * (sizeof(LedgerIndex) + sizeof(LedgerData) + kMapNodeOverhead);

        for (auto const& [_, ledgerData] : ledgers_)
        {
            size += ledgerData.transactions.size() *
                (sizeof(uint256) + sizeof(AccountTx) + kMapNodeOverhead);
            size += ledgerData.txOrder.capacity() * sizeof(uint256);
        }

        size +=
            ledgerHashToSeq_.size() * (sizeof(uint256) + sizeof(LedgerIndex) + kMapNodeOverhead);

        return size;
    }

    std::uint64_t
    getBytesUsedTransactionUnlocked() const
    {
        if (!useTxTables_)
            return 0;

        std::uint64_t size = 0;

        size += transactionMap_.size() * (sizeof(uint256) + sizeof(AccountTx) + kMapNodeOverhead);

        for (auto const& [_, accountTx] : transactionMap_)
        {
            if (accountTx.first)
                size += accountTx.first->getSTransaction()->getSerializer().peekData().size();
            if (accountTx.second)
                size += accountTx.second->getAsObject().getSerializer().peekData().size();
        }

        for (auto const& [accountId, accountData] : accountTxMap_)
        {
            size += sizeof(accountId) + sizeof(AccountTxData) + kMapNodeOverhead;
            for (auto const& [ledgerSeq, txVector] : accountData.ledgerTxMap)
            {
                size += sizeof(ledgerSeq) + kMapNodeOverhead;
                size += txVector.capacity() * sizeof(AccountTx);
            }
        }

        return size;
    }

public:
    std::uint32_t
    getKBUsedAll() override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);

        std::uint64_t const size =
            sizeof(*this) + getBytesUsedLedgerUnlocked() + getBytesUsedTransactionUnlocked();

        return static_cast<std::uint32_t>(size / 1024);
    }

    bool
    transactionDbHasSpace(Config const&) override
    {
        // In-memory database - always has space
        return true;
    }

    std::uint32_t
    getKBUsedLedger() override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        return static_cast<std::uint32_t>(getBytesUsedLedgerUnlocked() / 1024);
    }

    std::uint32_t
    getKBUsedTransaction() override
    {
        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        return static_cast<std::uint32_t>(getBytesUsedTransactionUnlocked() / 1024);
    }

    void
    closeLedgerDB() override
    {
    }

    void
    closeTransactionDB() override
    {
    }

    ~RWDBDatabase() override
    {
        accountTxMap_.clear();
        transactionMap_.clear();
        for (auto& ledger : ledgers_)
        {
            ledger.second.transactions.clear();
        }
        ledgers_.clear();
        ledgerHashToSeq_.clear();
    }

    std::vector<std::shared_ptr<Transaction>>
    getTxHistory(LedgerIndex startIndex) override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        std::vector<std::shared_ptr<Transaction>> result;

        LedgerIndex skipped = 0;
        int collected = 0;

        for (auto it = ledgers_.rbegin(); it != ledgers_.rend(); ++it)
        {
            auto const& transactions = it->second.transactions;
            for (auto const& txHash : it->second.txOrder)
            {
                auto const txIt = transactions.find(txHash);
                if (txIt == transactions.end())
                    continue;

                if (skipped < startIndex)
                {
                    ++skipped;
                    continue;
                }

                if (collected >= 20)
                    break;

                result.push_back(detachAccountTx(txIt->second).first);
                ++collected;
            }

            if (collected >= 20)
                break;
        }
        return result;
    }

    // Legacy/test-only. RPC uses accountTxPage; these ignore delegate
    // filters (AccountTxOptions has no delegate field).
    AccountTxs
    getOldestAccountTxs(AccountTxOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        AccountTxs result;
        auto const& accountData = it->second;
        auto [txIt, txEnd] = ledgerTxBounds(
            accountData.ledgerTxMap, options.ledgerRange.min, options.ledgerRange.max);
        auto const maxResults = accountTxResultLimit(options, false);

        std::size_t skipped = 0;
        for (; txIt != txEnd && result.size() < maxResults; ++txIt)
        {
            for (auto const& accountTx : txIt->second)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                result.push_back(detachAccountTx(accountTx));
                if (result.size() >= maxResults)
                    break;
            }
        }

        return result;
    }

    AccountTxs
    getNewestAccountTxs(AccountTxOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        AccountTxs result;
        auto const& accountData = it->second;
        auto [txIt, txEnd] = ledgerTxBounds(
            accountData.ledgerTxMap, options.ledgerRange.min, options.ledgerRange.max);
        auto const maxResults = accountTxResultLimit(options, false);

        std::size_t skipped = 0;
        for (auto rIt = std::make_reverse_iterator(txEnd);
             rIt != std::make_reverse_iterator(txIt) && result.size() < maxResults;
             ++rIt)
        {
            for (auto innerRIt = rIt->second.rbegin(); innerRIt != rIt->second.rend(); ++innerRIt)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                result.push_back(detachAccountTx(*innerRIt));
                if (result.size() >= maxResults)
                    break;
            }
        }

        return result;
    }

    MetaTxsList
    getOldestAccountTxsB(AccountTxOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        MetaTxsList result;
        auto const& accountData = it->second;
        auto [txIt, txEnd] = ledgerTxBounds(
            accountData.ledgerTxMap, options.ledgerRange.min, options.ledgerRange.max);
        auto const maxResults = accountTxResultLimit(options, true);

        std::size_t skipped = 0;
        for (; txIt != txEnd && result.size() < maxResults; ++txIt)
        {
            for (auto const& accountTx : txIt->second)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                auto const& [txn, txMeta] = accountTx;
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    txIt->first);
                if (result.size() >= maxResults)
                    break;
            }
        }

        return result;
    }

    MetaTxsList
    getNewestAccountTxsB(AccountTxOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        MetaTxsList result;
        auto const& accountData = it->second;
        auto [txIt, txEnd] = ledgerTxBounds(
            accountData.ledgerTxMap, options.ledgerRange.min, options.ledgerRange.max);
        auto const maxResults = accountTxResultLimit(options, true);

        std::size_t skipped = 0;
        for (auto rIt = std::make_reverse_iterator(txEnd);
             rIt != std::make_reverse_iterator(txIt) && result.size() < maxResults;
             ++rIt)
        {
            for (auto innerRIt = rIt->second.rbegin(); innerRIt != rIt->second.rend(); ++innerRIt)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                auto const& [txn, txMeta] = *innerRIt;
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    rIt->first);
                if (result.size() >= maxResults)
                    break;
            }
        }

        return result;
    }

    std::pair<std::optional<RelationalDatabase::AccountTxMarker>, int>
    accountTxPage(
        std::function<void(std::uint32_t)> const& onUnsavedLedger,
        std::function<void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const& onTransaction,
        RelationalDatabase::AccountTxPageOptions const& options,
        int limitUsed,
        std::uint32_t pageLength,
        bool forward)
    {
        struct EmittedTx
        {
            std::uint32_t ledgerSeq;
            Blob rawTxn;
            Blob rawMeta;
        };

        std::vector<EmittedTx> emitted;
        std::optional<RelationalDatabase::AccountTxMarker> newmarker;
        int total = 0;

        {
            std::shared_lock<ReaderPreferringSharedMutex> const lock(mutex_);
            auto it = accountTxMap_.find(options.account);
            if (it == accountTxMap_.end())
                return {std::nullopt, 0};

            bool lookingForMarker = options.marker.has_value();

            std::uint32_t numberOfResults = 0;

            if (options.limit == 0 || options.limit == UINT32_MAX ||
                (options.limit > pageLength && !options.bAdmin))
            {
                numberOfResults = pageLength;
            }
            else
            {
                numberOfResults = options.limit;
            }

            if (numberOfResults < limitUsed)
                return {options.marker, -1};
            numberOfResults -= limitUsed;

            std::uint32_t findLedger = 0, findSeq = 0;

            if (lookingForMarker)
            {
                findLedger = options.marker->ledgerSeq;
                findSeq = options.marker->txnSeq;
            }

            if (limitUsed > 0)
                newmarker = options.marker;

            // Cap the reservation at one page. Admin/unlimited callers
            // may pass a huge raw limit (anything except 0 / UINT32_MAX
            // is not clamped), and reserving that many EmittedTx slots
            // would allocate tens of GB before any row is found.
            emitted.reserve(std::min(numberOfResults, pageLength));
            std::optional<RelationalDatabase::AccountTxMarker> lastEmitted;

            bool const hasDelegateFilter = options.delegate.has_value();
            auto const txPasses = [&](AccountTx const& accountTx) {
                if (!hasDelegateFilter)
                    return true;
                auto const& stx = accountTx.first->getSTransaction();
                return stx && passesDelegateFilter(*stx, *options.delegate, options.account);
            };

            bool pageComplete = false;
            if (forward)
            {
                auto const& accountData = it->second;
                auto [txIt, txEnd] = ledgerTxBounds(
                    accountData.ledgerTxMap,
                    lookingForMarker ? clampMarkerToRange(findLedger, options.ledgerRange.min, true)
                                     : options.ledgerRange.min,
                    options.ledgerRange.max);
                for (; txIt != txEnd && !pageComplete; ++txIt)
                {
                    std::uint32_t const ledgerSeq = txIt->first;
                    // txnSeq is the index within this account's per-ledger
                    // vector, not the ledger's real TxnSeq. Markers are only
                    // interchangeable with other RWDB pages, not SQLite.
                    std::uint32_t txnSeq = 0;
                    for (auto const& accountTx : txIt->second)
                    {
                        if (lookingForMarker)
                        {
                            // Marker identifies the first unprocessed row.
                            // Skip strictly earlier rows; include the marker.
                            // If the marker ledger was pruned, resume at the
                            // first later ledger that is still present.
                            if (ledgerSeq < findLedger ||
                                (ledgerSeq == findLedger && txnSeq < findSeq))
                            {
                                ++txnSeq;
                                continue;
                            }
                            lookingForMarker = false;
                            // Delegate markers are last-emitted cursors; skip
                            // that row and resume at the next matching one.
                            if (hasDelegateFilter && ledgerSeq == findLedger && txnSeq == findSeq)
                            {
                                ++txnSeq;
                                continue;
                            }
                        }

                        if (!txPasses(accountTx))
                        {
                            ++txnSeq;
                            continue;
                        }

                        if (numberOfResults == 0)
                        {
                            // Non-delegate: first unprocessed row (included
                            // on resume). Delegate: last emitted row (skipped
                            // on resume), matching SQLite lastEmitted.
                            newmarker = hasDelegateFilter
                                ? lastEmitted
                                : RelationalDatabase::AccountTxMarker{
                                      .ledgerSeq = rangeCheckedCast<std::uint32_t>(ledgerSeq),
                                      .txnSeq = txnSeq};
                            pageComplete = true;
                            break;
                        }

                        emitted.push_back(
                            EmittedTx{
                                .ledgerSeq = rangeCheckedCast<std::uint32_t>(ledgerSeq),
                                .rawTxn =
                                    accountTx.first->getSTransaction()->getSerializer().peekData(),
                                .rawMeta =
                                    accountTx.second->getAsObject().getSerializer().peekData()});
                        --numberOfResults;
                        ++total;
                        lastEmitted = {
                            .ledgerSeq = rangeCheckedCast<std::uint32_t>(ledgerSeq),
                            .txnSeq = txnSeq};
                        ++txnSeq;
                    }
                }
            }
            else
            {
                auto const& accountData = it->second;
                auto [txIt, txEnd] = ledgerTxBounds(
                    accountData.ledgerTxMap,
                    options.ledgerRange.min,
                    lookingForMarker
                        ? clampMarkerToRange(findLedger, options.ledgerRange.max, false)
                        : options.ledgerRange.max);
                auto rtxIt = std::make_reverse_iterator(txEnd);
                auto rtxEnd = std::make_reverse_iterator(txIt);
                for (; rtxIt != rtxEnd && !pageComplete; ++rtxIt)
                {
                    std::uint32_t const ledgerSeq = rtxIt->first;
                    if (rtxIt->second.empty())
                        continue;
                    std::uint32_t txnSeq = rtxIt->second.size() - 1;
                    for (auto innerRIt = rtxIt->second.rbegin(); innerRIt != rtxIt->second.rend();
                         ++innerRIt)
                    {
                        if (lookingForMarker)
                        {
                            // Reverse: skip strictly later rows; include marker.
                            // If the marker ledger was pruned, resume at the
                            // first earlier ledger that is still present.
                            if (ledgerSeq > findLedger ||
                                (ledgerSeq == findLedger && txnSeq > findSeq))
                            {
                                if (txnSeq > 0)
                                    --txnSeq;
                                continue;
                            }
                            lookingForMarker = false;
                            if (hasDelegateFilter && ledgerSeq == findLedger && txnSeq == findSeq)
                            {
                                if (txnSeq > 0)
                                    --txnSeq;
                                continue;
                            }
                        }

                        auto const& accountTx = *innerRIt;
                        if (!txPasses(accountTx))
                        {
                            if (txnSeq > 0)
                                --txnSeq;
                            continue;
                        }

                        if (numberOfResults == 0)
                        {
                            newmarker = hasDelegateFilter
                                ? lastEmitted
                                : RelationalDatabase::AccountTxMarker{
                                      .ledgerSeq = rangeCheckedCast<std::uint32_t>(ledgerSeq),
                                      .txnSeq = txnSeq};
                            pageComplete = true;
                            break;
                        }

                        emitted.push_back(
                            EmittedTx{
                                .ledgerSeq = rangeCheckedCast<std::uint32_t>(ledgerSeq),
                                .rawTxn =
                                    accountTx.first->getSTransaction()->getSerializer().peekData(),
                                .rawMeta =
                                    accountTx.second->getAsObject().getSerializer().peekData()});
                        --numberOfResults;
                        ++total;
                        lastEmitted = {
                            .ledgerSeq = rangeCheckedCast<std::uint32_t>(ledgerSeq),
                            .txnSeq = txnSeq};
                        if (txnSeq > 0)
                            --txnSeq;
                    }
                }
            }
        }

        // Callbacks run without mutex_ so a JobQueue fallback into
        // saveValidatedLedger cannot unique_lock the same shared_mutex.
        for (auto& row : emitted)
        {
            if (row.rawMeta.empty())
                onUnsavedLedger(row.ledgerSeq);
            onTransaction(
                row.ledgerSeq,
                std::string(1, static_cast<char>(TxnSql::Validated)),
                std::move(row.rawTxn),
                std::move(row.rawMeta));
        }
        return {newmarker, total};
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        static std::uint32_t const kPageLength(200);
        auto onUnsavedLedger = std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
        AccountTxs ret;
        Application& app = app_;
        auto onTransaction = [&ret, &app](
                                 std::uint32_t ledgerIndex,
                                 std::string const& status,
                                 Blob&& rawTxn,
                                 Blob&& rawMeta) {
            convertBlobsToTxResult(ret, ledgerIndex, status, rawTxn, rawMeta, app);
        };

        auto newmarker =
            accountTxPage(onUnsavedLedger, onTransaction, options, 0, kPageLength, true).first;
        return {ret, newmarker};
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        static std::uint32_t const kPageLength(200);
        auto onUnsavedLedger = std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
        AccountTxs ret;
        Application& app = app_;
        auto onTransaction = [&ret, &app](
                                 std::uint32_t ledgerIndex,
                                 std::string const& status,
                                 Blob&& rawTxn,
                                 Blob&& rawMeta) {
            convertBlobsToTxResult(ret, ledgerIndex, status, rawTxn, rawMeta, app);
        };

        auto newmarker =
            accountTxPage(onUnsavedLedger, onTransaction, options, 0, kPageLength, false).first;
        return {ret, newmarker};
    }

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        static std::uint32_t const kPageLength(500);
        auto onUnsavedLedger = std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
        MetaTxsList ret;
        auto onTransaction = [&ret](
                                 std::uint32_t ledgerIndex,
                                 std::string const& status,
                                 Blob&& rawTxn,
                                 Blob&& rawMeta) {
            ret.emplace_back(std::move(rawTxn), std::move(rawMeta), ledgerIndex);
        };
        auto newmarker =
            accountTxPage(onUnsavedLedger, onTransaction, options, 0, kPageLength, true).first;
        return {ret, newmarker};
    }

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        static std::uint32_t const kPageLength(500);
        auto onUnsavedLedger = std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
        MetaTxsList ret;
        auto onTransaction = [&ret](
                                 std::uint32_t ledgerIndex,
                                 std::string const& status,
                                 Blob&& rawTxn,
                                 Blob&& rawMeta) {
            ret.emplace_back(std::move(rawTxn), std::move(rawMeta), ledgerIndex);
        };
        auto newmarker =
            accountTxPage(onUnsavedLedger, onTransaction, options, 0, kPageLength, false).first;
        return {ret, newmarker};
    }
};

}  // namespace xrpl
