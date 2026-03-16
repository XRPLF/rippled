#pragma once

#include <xrpld/app/ledger/AcceptedLedger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/PendingSaves.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/detail/AccountTxPaging.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpld/core/Config.h>

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
        std::map<uint256, AccountTx> transactions;
        bool transactionsPurged{false};
    };

    struct AccountTxData
    {
        std::map<uint32_t, std::vector<AccountTx>>
            ledgerTxMap;  // ledgerSeq -> vector of transactions
    };

    Application& app_;
    bool const useTxTables_;

    mutable std::shared_mutex mutex_;

    std::map<LedgerIndex, LedgerData> ledgers_;
    std::map<uint256, LedgerIndex> ledgerHashToSeq_;
    std::map<uint256, AccountTx> transactionMap_;
    std::map<AccountID, AccountTxData> accountTxMap_;

public:
    RWDBDatabase(ServiceRegistry& registry, Config const& config, JobQueue&)
        : app_(registry.app())
        , useTxTables_(config.useTxTables())
    {
    }

    std::optional<LedgerIndex>
    getMinLedgerSeq() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.begin()->first;
    }

    std::optional<LedgerIndex>
    getTransactionsMinLedgerSeq() override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto& [ledgerSeq, ledgerData] : ledgers_)
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

        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (accountTxMap_.empty())
            return std::nullopt;
        LedgerIndex minSeq = std::numeric_limits<LedgerIndex>::max();
        for (const auto& [_, accountData] : accountTxMap_)
        {
            if (!accountData.ledgerTxMap.empty())
                minSeq =
                    std::min(minSeq, accountData.ledgerTxMap.begin()->first);
        }
        return minSeq == std::numeric_limits<LedgerIndex>::max()
            ? std::nullopt
            : std::optional<LedgerIndex>(minSeq);
    }

    std::optional<LedgerIndex>
    getMaxLedgerSeq() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.rbegin()->first;
    }

    void
    deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) override
    {
        if (!useTxTables_)
            return;

        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerSeq);
        if (it != ledgers_.end())
        {
            for (const auto& [txHash, _] : it->second.transactions)
            {
                transactionMap_.erase(txHash);
            }
            it->second.transactions.clear();
            it->second.transactionsPurged = true;
        }
    }

    void
    deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.begin();
        while (it != ledgers_.end() && it->first < ledgerSeq)
        {
            if (useTxTables_)
            {
                // Purge per-ledger transaction index before removing the ledger.
                for (const auto& [txHash, _] : it->second.transactions)
                    transactionMap_.erase(txHash);
            }
            ledgerHashToSeq_.erase(it->second.info.hash);
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
                accountIt = accountTxMap_.erase(accountIt);
            else
                ++accountIt;
        }
    }

    void
    deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        if (!useTxTables_)
            return;

        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.begin();
        while (it != ledgers_.end() && it->first < ledgerSeq)
        {
            for (const auto& [txHash, _] : it->second.transactions)
            {
                transactionMap_.erase(txHash);
            }
            it->second.transactions.clear();
            it->second.transactionsPurged = true;
            ++it;
        }
    }

    void
    deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        if (!useTxTables_)
            return;

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto accountIt = accountTxMap_.begin(); accountIt != accountTxMap_.end();)
        {
            auto& accountData = accountIt->second;
            auto txIt = accountData.ledgerTxMap.begin();
            while (txIt != accountData.ledgerTxMap.end() &&
                   txIt->first < ledgerSeq)
            {
                txIt = accountData.ledgerTxMap.erase(txIt);
            }

            if (accountData.ledgerTxMap.empty())
                accountIt = accountTxMap_.erase(accountIt);
            else
                ++accountIt;
        }
    }

    std::size_t
    getTransactionCount() override
    {
        if (!useTxTables_)
            return 0;

        std::shared_lock<std::shared_mutex> lock(mutex_);
        return transactionMap_.size();
    }

    std::size_t
    getAccountTransactionCount() override
    {
        if (!useTxTables_)
            return 0;

        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::size_t count = 0;
        for (const auto& [_, accountData] : accountTxMap_)
        {
            for (const auto& [_, txVector] : accountData.ledgerTxMap)
            {
                count += txVector.size();
            }
        }
        return count;
    }

    CountMinMax
    getLedgerCountMinMax() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return {0, 0, 0};
        return {
            ledgers_.size(), ledgers_.begin()->first, ledgers_.rbegin()->first};
    }

    bool
    saveValidatedLedger(
        std::shared_ptr<Ledger const> const& ledger,
        bool current) override
    {
        LedgerData ledgerData;
        ledgerData.info = ledger->header();
        ledgerData.transactionsPurged = false;
        auto j = app_.journal("Ledger");
        auto seq = ledger->header().seq;

        JLOG(j.trace()) << "saveValidatedLedger "
                        << (current ? "" : "fromAcquire ") << seq;

        if (!ledger->header().accountHash.isNonZero())
        {
            JLOG(j.fatal())
                << "AH is zero: " << getJson({*ledger, {}}).asString();
            UNREACHABLE(
                "RWDBDatabase::saveValidatedLedger : account hash is zero");
        }

        if (ledger->header().accountHash !=
            ledger->stateMap().getHash().as_uint256())
        {
            JLOG(j.fatal()) << "sAL: " << ledger->header().accountHash
                            << " != " << ledger->stateMap().getHash();
            JLOG(j.fatal())
                << "saveAcceptedLedger: seq=" << seq << ", current=" << current;
            UNREACHABLE(
                "RWDBDatabase::saveValidatedLedger : account hash mismatch");
        }

        XRPL_ASSERT(
            ledger->header().txHash == ledger->txMap().getHash().as_uint256(),
            "RWDBDatabase::saveValidatedLedger : tx hash mismatch");

        {
            Serializer s(128);
            s.add32(HashPrefix::ledgerMaster);
            addRaw(ledger->header(), s);
            app_.getNodeStore().store(
                hotLEDGER, std::move(s.modData()), ledger->header().hash, seq);
        }

        std::shared_ptr<AcceptedLedger> aLedger;
        try
        {
            aLedger = app_.getAcceptedLedgerCache().fetch(ledger->header().hash);
            if (!aLedger)
            {
                aLedger = std::make_shared<AcceptedLedger>(ledger);
                app_.getAcceptedLedgerCache().canonicalize_replace_client(
                    ledger->header().hash, aLedger);
            }
        }
        catch (std::exception const&)
        {
            JLOG(j.warn()) << "An accepted ledger was missing nodes";
            app_.getLedgerMaster().failedSave(seq, ledger->header().hash);
            app_.pendingSaves().finishWork(seq);
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
                std::string reason;

                auto accTx = std::make_pair(
                    std::make_shared<Transaction>(txn, reason, app_),
                    std::make_shared<TxMeta>(meta));

                // Initialize once at insert time to avoid mutating shared
                // Transaction instances from concurrent read paths.
                accTx.first->setStatus(COMMITTED);
                accTx.first->setLedger(seq);

                txInserts.push_back(
                    TxInsert{
                        id,
                        accTx,
                        std::vector<AccountID>(
                            meta.getAffectedAccounts().begin(),
                            meta.getAffectedAccounts().end()),
                        acceptedLedgerTx->getTxnSeq()});
            }

            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                for (auto const& insert : txInserts)
                {
                    ledgerData.transactions.emplace(insert.id, insert.accTx);
                    transactionMap_.emplace(insert.id, insert.accTx);

                    for (auto const& account : insert.affected)
                    {
                        if (accountTxMap_.find(account) == accountTxMap_.end())
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
                    insert.id,
                    seq,
                    insert.txnSeq,
                    app_.config().NETWORK_ID);
            }
            return true;
        }

        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            ledgers_[seq] = std::move(ledgerData);
            ledgerHashToSeq_[ledger->header().hash] = seq;
        }
        return true;
    }

    std::optional<LedgerHeader>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerSeq);
        if (it != ledgers_.end())
            return it->second.info;
        return std::nullopt;
    }

    std::optional<LedgerHeader>
    getNewestLedgerInfo() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.rbegin()->second.info;
    }

    std::optional<LedgerHeader>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.lower_bound(ledgerFirstIndex);
        if (it != ledgers_.end())
            return it->second.info;
        return std::nullopt;
    }

    std::optional<LedgerHeader>
    getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.lower_bound(ledgerFirstIndex);
        if (it == ledgers_.end())
            return std::nullopt;
        return ledgers_.rbegin()->second.info;
    }

    std::optional<LedgerHeader>
    getLedgerInfoByHash(uint256 const& ledgerHash) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgerHashToSeq_.find(ledgerHash);
        if (it != ledgerHashToSeq_.end())
            return ledgers_.at(it->second).info;
        return std::nullopt;
    }

    uint256
    getHashByIndex(LedgerIndex ledgerIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerIndex);
        if (it != ledgers_.end())
            return it->second.info.hash;
        return uint256();
    }

    std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerIndex);
        if (it != ledgers_.end())
        {
            return LedgerHashPair{
                it->second.info.hash, it->second.info.parentHash};
        }
        return std::nullopt;
    }

    std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::map<LedgerIndex, LedgerHashPair> result;
        auto it = ledgers_.lower_bound(minSeq);
        auto end = ledgers_.upper_bound(maxSeq);
        for (; it != end; ++it)
        {
            result[it->first] = LedgerHashPair{
                it->second.info.hash, it->second.info.parentHash};
        }
        return result;
    }

    std::variant<AccountTx, TxSearched>
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<std::uint32_t>> const& range,
        error_code_i& ec) override
    {
        if (!useTxTables_)
            return TxSearched::unknown;

        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = transactionMap_.find(id);
        if (it != transactionMap_.end())
        {
            return it->second;
        }

        if (range)
        {
            std::size_t count = 0;
            for (LedgerIndex seq = range->first(); seq <= range->last(); ++seq)
            {
                auto found = ledgers_.find(seq);
                if (found != ledgers_.end() && !found->second.transactionsPurged)
                    ++count;
            }
            return (count == (range->last() - range->first() + 1))
                ? TxSearched::all
                : TxSearched::some;
        }

        return TxSearched::unknown;
    }

    static constexpr size_t MAP_NODE_OVERHEAD = 40;

private:
    std::uint64_t
    getBytesUsedLedger_unlocked() const
    {
        std::uint64_t size = 0;

        size += ledgers_.size() *
            (sizeof(LedgerIndex) + sizeof(LedgerData) + MAP_NODE_OVERHEAD);

        for (const auto& [_, ledgerData] : ledgers_)
        {
            size += ledgerData.transactions.size() *
                (sizeof(uint256) + sizeof(AccountTx) + MAP_NODE_OVERHEAD);
        }

        size += ledgerHashToSeq_.size() *
            (sizeof(uint256) + sizeof(LedgerIndex) + MAP_NODE_OVERHEAD);

        return size;
    }

    std::uint64_t
    getBytesUsedTransaction_unlocked() const
    {
        if (!useTxTables_)
            return 0;

        std::uint64_t size = 0;

        size += transactionMap_.size() *
            (sizeof(uint256) + sizeof(AccountTx) + MAP_NODE_OVERHEAD);

        for (const auto& [_, accountTx] : transactionMap_)
        {
            if (accountTx.first)
                size += accountTx.first->getSTransaction()
                            ->getSerializer()
                            .peekData()
                            .size();
            if (accountTx.second)
                size += accountTx.second->getAsObject()
                            .getSerializer()
                            .peekData()
                            .size();
        }

        for (const auto& [accountId, accountData] : accountTxMap_)
        {
            size +=
                sizeof(accountId) + sizeof(AccountTxData) + MAP_NODE_OVERHEAD;
            for (const auto& [ledgerSeq, txVector] : accountData.ledgerTxMap)
            {
                size += sizeof(ledgerSeq) + MAP_NODE_OVERHEAD;
                size += txVector.capacity() * sizeof(AccountTx);
            }
        }

        return size;
    }

public:
    std::uint32_t
    getKBUsedAll() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        std::uint64_t size = sizeof(*this) + getBytesUsedLedger_unlocked() +
            getBytesUsedTransaction_unlocked();

        return static_cast<std::uint32_t>(size / 1024);
    }

    std::uint32_t
    getKBUsedLedger() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return static_cast<std::uint32_t>(getBytesUsedLedger_unlocked() / 1024);
    }

    std::uint32_t
    getKBUsedTransaction() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return static_cast<std::uint32_t>(
            getBytesUsedTransaction_unlocked() / 1024);
    }

    void
    closeLedgerDB() override
    {
    }

    void
    closeTransactionDB() override
    {
    }

    ~RWDBDatabase()
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

        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::shared_ptr<Transaction>> result;

        LedgerIndex skipped = 0;
        int collected = 0;

        for (auto it = ledgers_.rbegin(); it != ledgers_.rend(); ++it)
        {
            const auto& transactions = it->second.transactions;
            for (const auto& [txHash, accountTx] : transactions)
            {
                if (skipped < startIndex)
                {
                    ++skipped;
                    continue;
                }

                if (collected >= 20)
                {
                    break;
                }

                result.push_back(accountTx.first);
                ++collected;
            }

            if (collected >= 20)
                break;
        }
        return result;
    }

    AccountTxs
    getOldestAccountTxs(AccountTxOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        AccountTxs result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (; txIt != txEnd &&
             (options.bUnlimited || result.size() < options.limit);
             ++txIt)
        {
            for (const auto& accountTx : txIt->second)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                result.push_back(accountTx);
                if (!options.bUnlimited && result.size() >= options.limit)
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

        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        AccountTxs result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (auto rIt = std::make_reverse_iterator(txEnd);
             rIt != std::make_reverse_iterator(txIt) &&
             (options.bUnlimited || result.size() < options.limit);
             ++rIt)
        {
            for (auto innerRIt = rIt->second.rbegin();
                 innerRIt != rIt->second.rend();
                 ++innerRIt)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                AccountTx const accountTx = *innerRIt;
                result.push_back(accountTx);
                if (!options.bUnlimited && result.size() >= options.limit)
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

        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        MetaTxsList result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (; txIt != txEnd &&
             (options.bUnlimited || result.size() < options.limit);
             ++txIt)
        {
            for (const auto& accountTx : txIt->second)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                const auto& [txn, txMeta] = accountTx;
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    txIt->first);
                if (!options.bUnlimited && result.size() >= options.limit)
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

        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        MetaTxsList result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (auto rIt = std::make_reverse_iterator(txEnd);
             rIt != std::make_reverse_iterator(txIt) &&
             (options.bUnlimited || result.size() < options.limit);
             ++rIt)
        {
            for (auto innerRIt = rIt->second.rbegin();
                 innerRIt != rIt->second.rend();
                 ++innerRIt)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                const auto& [txn, txMeta] = *innerRIt;
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    rIt->first);
                if (!options.bUnlimited && result.size() >= options.limit)
                    break;
            }
        }

        return result;
    }

    std::pair<std::optional<RelationalDatabase::AccountTxMarker>, int>
    accountTxPage(
        std::function<void(std::uint32_t)> const& onUnsavedLedger,
        std::function<
            void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
            onTransaction,
        RelationalDatabase::AccountTxPageOptions const& options,
        int limit_used,
        std::uint32_t page_length,
        bool forward)
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {std::nullopt, 0};

        int total = 0;

        bool lookingForMarker = options.marker.has_value();

        std::uint32_t numberOfResults;

        if (options.limit == 0 || options.limit == UINT32_MAX ||
            (options.limit > page_length && !options.bAdmin))
            numberOfResults = page_length;
        else
            numberOfResults = options.limit;

        if (numberOfResults < limit_used)
            return {options.marker, -1};
        numberOfResults -= limit_used;

        std::uint32_t findLedger = 0, findSeq = 0;

        if (lookingForMarker)
        {
            findLedger = options.marker->ledgerSeq;
            findSeq = options.marker->txnSeq;
        }

        std::optional<RelationalDatabase::AccountTxMarker> newmarker;
        if (limit_used > 0)
            newmarker = options.marker;

        if (forward)
        {
            const auto& accountData = it->second;
            auto txIt = accountData.ledgerTxMap.lower_bound(
                findLedger == 0 ? options.minLedger : findLedger);
            auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);
            for (; txIt != txEnd; ++txIt)
            {
                std::uint32_t const ledgerSeq = txIt->first;
                std::uint32_t txnSeq = 0;
                for (const auto& accountTx : txIt->second)
                {
                    if (lookingForMarker)
                    {
                        // Marker semantics: marker identifies the first
                        // unprocessed row. Resume by skipping strictly earlier
                        // rows; include the marker row itself.
                        // If marker ledger was pruned by online_delete, begin
                        // from the first available later ledger.
                        if (ledgerSeq < findLedger ||
                            (ledgerSeq == findLedger && txnSeq < findSeq))
                        {
                            ++txnSeq;
                            continue;
                        }
                        lookingForMarker = false;
                    }
                    else if (numberOfResults == 0)
                    {
                        newmarker = {
                            rangeCheckedCast<std::uint32_t>(ledgerSeq), txnSeq};
                        return {newmarker, total};
                    }

                    Blob rawTxn = accountTx.first->getSTransaction()
                                      ->getSerializer()
                                      .peekData();
                    Blob rawMeta = accountTx.second->getAsObject()
                                       .getSerializer()
                                       .peekData();

                    if (rawMeta.size() == 0)
                        onUnsavedLedger(ledgerSeq);

                    onTransaction(
                        rangeCheckedCast<std::uint32_t>(ledgerSeq),
                        "COMMITTED",
                        std::move(rawTxn),
                        std::move(rawMeta));
                    --numberOfResults;
                    ++total;
                    ++txnSeq;
                }
            }
        }
        else
        {
            const auto& accountData = it->second;
            auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
            auto txEnd = accountData.ledgerTxMap.upper_bound(
                findLedger == 0 ? options.maxLedger : findLedger);
            auto rtxIt = std::make_reverse_iterator(txEnd);
            auto rtxEnd = std::make_reverse_iterator(txIt);
            for (; rtxIt != rtxEnd; ++rtxIt)
            {
                std::uint32_t const ledgerSeq = rtxIt->first;
                if (rtxIt->second.empty())
                    continue;
                std::uint32_t txnSeq = rtxIt->second.size() - 1;
                for (auto innerRIt = rtxIt->second.rbegin();
                     innerRIt != rtxIt->second.rend();
                     ++innerRIt)
                {
                    if (lookingForMarker)
                    {
                        // Reverse marker semantics: marker identifies the
                        // first unprocessed row. Resume by skipping strictly
                        // later rows; include the marker row itself.
                        // If marker ledger was pruned by online_delete, begin
                        // from the first available earlier ledger.
                        if (ledgerSeq > findLedger ||
                            (ledgerSeq == findLedger && txnSeq > findSeq))
                        {
                            if (txnSeq > 0)
                                --txnSeq;
                            continue;
                        }
                        lookingForMarker = false;
                    }
                    else if (numberOfResults == 0)
                    {
                        newmarker = {
                            rangeCheckedCast<std::uint32_t>(ledgerSeq), txnSeq};
                        return {newmarker, total};
                    }

                    const auto& accountTx = *innerRIt;
                    Blob rawTxn = accountTx.first->getSTransaction()
                                      ->getSerializer()
                                      .peekData();
                    Blob rawMeta = accountTx.second->getAsObject()
                                       .getSerializer()
                                       .peekData();

                    if (rawMeta.size() == 0)
                        onUnsavedLedger(ledgerSeq);

                    onTransaction(
                        rangeCheckedCast<std::uint32_t>(ledgerSeq),
                        "COMMITTED",
                        std::move(rawTxn),
                        std::move(rawMeta));
                    --numberOfResults;
                    ++total;
                    if (txnSeq > 0)
                        --txnSeq;
                }
            }
        }
        return {newmarker, total};
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        static std::uint32_t const page_length(200);
        auto onUnsavedLedger =
            std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
        AccountTxs ret;
        Application& app = app_;
        auto onTransaction = [&ret, &app](
                                 std::uint32_t ledger_index,
                                 std::string const& status,
                                 Blob&& rawTxn,
                                 Blob&& rawMeta) {
            convertBlobsToTxResult(
                ret, ledger_index, status, rawTxn, rawMeta, app);
        };

        auto newmarker =
            accountTxPage(
                onUnsavedLedger, onTransaction, options, 0, page_length, true)
                .first;
        return {ret, newmarker};
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        static std::uint32_t const page_length(200);
        auto onUnsavedLedger =
            std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
        AccountTxs ret;
        Application& app = app_;
        auto onTransaction = [&ret, &app](
                                 std::uint32_t ledger_index,
                                 std::string const& status,
                                 Blob&& rawTxn,
                                 Blob&& rawMeta) {
            convertBlobsToTxResult(
                ret, ledger_index, status, rawTxn, rawMeta, app);
        };

        auto newmarker =
            accountTxPage(
                onUnsavedLedger, onTransaction, options, 0, page_length, false)
                .first;
        return {ret, newmarker};
    }

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        static std::uint32_t const page_length(500);
        auto onUnsavedLedger =
            std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
        MetaTxsList ret;
        auto onTransaction = [&ret](
                                 std::uint32_t ledgerIndex,
                                 std::string const& status,
                                 Blob&& rawTxn,
                                 Blob&& rawMeta) {
            ret.emplace_back(
                std::move(rawTxn), std::move(rawMeta), ledgerIndex);
        };
        auto newmarker =
            accountTxPage(
                onUnsavedLedger, onTransaction, options, 0, page_length, true)
                .first;
        return {ret, newmarker};
    }

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) override
    {
        if (!useTxTables_)
            return {};

        static std::uint32_t const page_length(500);
        auto onUnsavedLedger =
            std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
        MetaTxsList ret;
        auto onTransaction = [&ret](
                                 std::uint32_t ledgerIndex,
                                 std::string const& status,
                                 Blob&& rawTxn,
                                 Blob&& rawMeta) {
            ret.emplace_back(
                std::move(rawTxn), std::move(rawMeta), ledgerIndex);
        };
        auto newmarker =
            accountTxPage(
                onUnsavedLedger, onTransaction, options, 0, page_length, false)
                .first;
        return {ret, newmarker};
    }
};

}  // namespace xrpl
