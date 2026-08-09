#include <xrpld/rpc/detail/AssetCache.h>

#include <xrpld/rpc/detail/MPT.h>
#include <xrpld/rpc/detail/TrustLine.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace xrpl {
namespace {

// Thread-local session id for automatic pin on getRippleLines (set by SessionPin).
thread_local int tlsPinSessionId = 0;

// Thread-local chunk override from LoadScope. 0 = use AssetCache::lineChunkSize_.
thread_local std::size_t tlsChunkOverride = 0;

}  // namespace

AssetCache::SessionPin::SessionPin(int sessionId) noexcept : prev_(tlsPinSessionId)
{
    tlsPinSessionId = sessionId;
}

AssetCache::SessionPin::~SessionPin() noexcept
{
    tlsPinSessionId = prev_;
}

AssetCache::LoadScope::LoadScope(std::size_t chunkLines) noexcept : prev_(tlsChunkOverride)
{
    tlsChunkOverride = chunkLines == 0 ? 0 : chunkLines;
}

AssetCache::LoadScope::~LoadScope() noexcept
{
    tlsChunkOverride = prev_;
}

AssetCache::AssetCache(
    std::shared_ptr<ReadView const> ledger,
    beast::Journal j,
    std::size_t maxTotalLines,
    std::size_t maxLinesPerAccount,
    std::uint32_t cacheReuseLedgers,
    std::size_t lineChunkSize)
    : ledger_(std::move(ledger))
    , journal_(j)
    , maxTotalLines_(maxTotalLines)
    , maxLinesPerAccount_(maxLinesPerAccount)
    , cacheReuseLedgers_(cacheReuseLedgers)
    , lineChunkSize_(lineChunkSize == 0 ? rpc::tuning::kPathFindLineChunkSize : lineChunkSize)
{
    JLOG(journal_.debug()) << "created for ledger " << ledger_->header().seq
                           << " maxTotalLines=" << maxTotalLines_
                           << " maxLinesPerAccount=" << maxLinesPerAccount_
                           << " cacheReuseLedgers=" << cacheReuseLedgers_
                           << " lineChunkSize=" << lineChunkSize_;
}

AssetCache::~AssetCache()
{
    JLOG(journal_.debug()) << "destroyed for ledger " << ledger_->header().seq << " with "
                           << lines_.size() << " accounts and "
                           << totalLineCount_.load(std::memory_order_relaxed)
                           << " trust lines (hits=" << cacheHits_.load(std::memory_order_relaxed)
                           << " misses=" << cacheMisses_.load(std::memory_order_relaxed)
                           << " loaded=" << linesLoaded_.load(std::memory_order_relaxed)
                           << " advances=" << ledgerAdvances_.load(std::memory_order_relaxed)
                           << ")";
}

std::shared_ptr<ReadView const>
AssetCache::getLedger() const
{
    std::shared_lock const sl(lock_);
    return ledger_;
}

void
AssetCache::advanceLedger(std::shared_ptr<ReadView const> const& ledger, bool forceClear)
{
    std::unique_lock const sl(lock_);
    if (!ledger)
        return;

    auto const oldSeq = ledger_->header().seq;
    auto const newSeq = ledger->header().seq;
    // Same-seq open → closed is a real view upgrade (mid-close then close).
    // Same-seq closed → open / identical open is a no-op without forceClear.
    bool const sameSeqUpgrade = oldSeq == newSeq && ledger_->open() && !ledger->open();
    if (oldSeq == newSeq && !forceClear && !sameSeqUpgrade)
        return;

    ledger_ = ledger;
    ++ledgerAdvances_;

    // MPTs are cheap; always drop on advance.
    mpts_.clear();

    if (forceClear)
    {
        lines_.clear();
        sessionAccounts_.clear();
        totalLineCount_.store(0, std::memory_order_relaxed);
        JLOG(journal_.info()) << "advanceLedger force-cleared cache for ledger " << newSeq;
        return;
    }

    // Soft retain complete vectors. Incomplete progressive fills cannot resume
    // a DirCursor across ledgers: owner-dir pages split/merge, so {page,
    // indexInPage} from an older ledger can dup or skip lines. Drop partial
    // entries (pins re-established on next getRippleLines via SessionPin).
    for (auto it = lines_.begin(); it != lines_.end();)
    {
        if (it->second.cursor.complete)
        {
            ++it;
            continue;
        }
        auto const n = it->second.storedLineCount();
        if (n > 0)
            totalLineCount_.fetch_sub(n, std::memory_order_relaxed);
        it = lines_.erase(it);
    }

    // Complete vectors + sessionAccounts_ pins kept; incomplete reloaded next get.
    JLOG(journal_.debug()) << "advanceLedger " << oldSeq << " -> " << newSeq
                           << (sameSeqUpgrade ? " (open->closed)" : "") << " retained "
                           << lines_.size() << " accounts / "
                           << totalLineCount_.load(std::memory_order_relaxed) << " lines";
}

std::size_t
AssetCache::effectiveChunkSize() const
{
    return tlsChunkOverride != 0 ? tlsChunkOverride : lineChunkSize_;
}

void
AssetCache::pinAccountUnlocked(int sessionId, AccountID const& accountID)
{
    // First pin of this account by this session increments pinCount.
    auto& held = sessionAccounts_[sessionId];
    if (!held.insert(accountID).second)
        return;  // already pinned by this session

    auto it = lines_.find(accountID);
    if (it == lines_.end())
    {
        // Should not happen: pin only after load. Roll back session set entry.
        held.erase(accountID);
        if (held.empty())
            sessionAccounts_.erase(sessionId);
        return;
    }
    ++it->second.pinCount;
}

std::size_t
AssetCache::remainingBudgetUnlocked() const
{
    auto const total = totalLineCount_.load(std::memory_order_relaxed);
    return maxTotalLines_ > total ? maxTotalLines_ - total : 0;
}

void
AssetCache::coalescePendingUnlocked(LineEntry& entry)
{
    if (entry.pending.empty())
        return;

    auto appendPending = [&](std::vector<PathFindTrustLine>& dest) {
        for (auto& part : entry.pending)
        {
            if (!part)
                continue;
            dest.reserve(dest.size() + part->size());
            for (auto& line : *part)
                dest.push_back(std::move(line));
        }
        entry.pending.clear();
    };

    if (!entry.lines)
    {
        entry.lines = std::make_shared<std::vector<PathFindTrustLine>>();
        appendPending(*entry.lines);
        return;
    }

    // Sole owner: absorb pending in place (no full duplicate of published lines).
    if (entry.lines.use_count() == 1)
    {
        appendPending(*entry.lines);
        return;
    }

    // Still shared with a reader: publish a new vector for future callers.
    // Prior readers keep their stable snapshot. Peak cost is paid once on
    // first publish after concurrent expand — never on the expand itself.
    auto grown = std::make_shared<std::vector<PathFindTrustLine>>();
    grown->reserve(entry.storedLineCount());
    for (auto const& line : *entry.lines)
        grown->push_back(line);
    appendPending(*grown);
    entry.lines = std::move(grown);
}

std::size_t
AssetCache::expandAccountUnlocked(AccountID const& accountID, LineEntry& entry)
{
    if (entry.cursor.complete)
        return 0;

    auto const have = entry.storedLineCount();
    if (have >= maxLinesPerAccount_)
    {
        entry.cursor.complete = true;
        return 0;
    }

    auto const remaining = remainingBudgetUnlocked();
    if (remaining == 0)
        return 0;

    // Expand size follows LoadScope when set; otherwise configured lineChunkSize_
    // (WS slow load). One-shot sets a large LoadScope to finish in one pass.
    std::size_t want = effectiveChunkSize();
    want = std::min(want, remaining);
    want = std::min(want, maxLinesPerAccount_ - have);
    if (want == 0)
        return 0;

    auto chunk = PathFindTrustLine::getItemsChunk(
        accountID, *ledger_, LineDirection::Outgoing, entry.cursor, want);

    entry.cursor = chunk.cursor;

    if (chunk.lines.empty())
    {
        // No matching lines in this span; cursor still advanced / completed.
        if (have >= maxLinesPerAccount_)
            entry.cursor.complete = true;
        return 0;
    }

    auto const added = chunk.lines.size();
    // Never full-copy the published vector on expand:
    // - sole owner → append in place (after absorbing any pending)
    // - shared with readers → push a pending chunk only (+chunk memory)
    // PathFindTrustLine is constructible but not assignable — only push_back.
    if (!entry.lines && entry.pending.empty())
    {
        entry.lines = std::make_shared<std::vector<PathFindTrustLine>>(std::move(chunk.lines));
    }
    else if (entry.lines && entry.lines.use_count() == 1)
    {
        if (!entry.pending.empty())
            coalescePendingUnlocked(entry);
        entry.lines->reserve(entry.lines->size() + added);
        for (auto& line : chunk.lines)
            entry.lines->push_back(std::move(line));
    }
    else
    {
        entry.pending.push_back(
            std::make_shared<std::vector<PathFindTrustLine>>(std::move(chunk.lines)));
    }

    totalLineCount_.fetch_add(added, std::memory_order_relaxed);
    linesLoaded_.fetch_add(added, std::memory_order_relaxed);
    lineEpoch_.fetch_add(1, std::memory_order_relaxed);

    if (entry.storedLineCount() >= maxLinesPerAccount_)
        entry.cursor.complete = true;

    if (!entry.cursor.complete)
    {
        JLOG(journal_.debug()) << "expandAccount partial account=" << accountID
                               << " lines=" << entry.storedLineCount()
                               << " pending_chunks=" << entry.pending.size()
                               << " page=" << entry.cursor.page
                               << " idx=" << entry.cursor.indexInPage;
    }

    return added;
}

std::shared_ptr<std::vector<PathFindTrustLine>>
AssetCache::loadOutgoingUnlocked(AccountID const& accountID)
{
    // Caller holds unique lock_.
    auto const curSeq = ledger_->header().seq;
    auto it = lines_.find(accountID);

    std::size_t preservedPins = 0;
    if (it != lines_.end())
    {
        auto const age = curSeq >= it->second.loadedSeq ? curSeq - it->second.loadedSeq : curSeq;
        if (age <= cacheReuseLedgers_)
        {
            ++cacheHits_;
            return it->second.lines;
        }
        // Stale: drop content but preserve pinCount across reload.
        preservedPins = it->second.pinCount;
        auto const size = it->second.storedLineCount();
        totalLineCount_.fetch_sub(size, std::memory_order_relaxed);
        lines_.erase(it);
    }

    ++cacheMisses_;

    LineEntry entry;
    entry.loadedSeq = curSeq;
    entry.pinCount = preservedPins;
    entry.cursor = {};
    entry.lines = nullptr;
    entry.pending.clear();

    auto const remaining = remainingBudgetUnlocked();
    // First-load chunk: LoadScope override or configured lineChunkSize_.
    // No silent floor when remaining == 0 — leave incomplete empty entry so a
    // later expand can proceed if budget frees.
    std::size_t want = std::min(effectiveChunkSize(), maxLinesPerAccount_);
    if (remaining < want)
        want = remaining;

    if (want > 0)
    {
        auto chunk = PathFindTrustLine::getItemsChunk(
            accountID, *ledger_, LineDirection::Outgoing, entry.cursor, want);
        entry.cursor = chunk.cursor;
        if (!chunk.lines.empty())
        {
            entry.lines = std::make_shared<std::vector<PathFindTrustLine>>(std::move(chunk.lines));
            totalLineCount_.fetch_add(entry.lines->size(), std::memory_order_relaxed);
            linesLoaded_.fetch_add(entry.lines->size(), std::memory_order_relaxed);
            lineEpoch_.fetch_add(1, std::memory_order_relaxed);
            if (entry.lines->size() >= maxLinesPerAccount_)
                entry.cursor.complete = true;
        }
    }
    // else: remaining == 0 → empty, incomplete (cursor still at start)

    if (!entry.cursor.complete)
    {
        // Budget exhaustion is the surprising case; normal progressive chunks log at debug.
        if (remaining == 0 || remainingBudgetUnlocked() == 0)
        {
            JLOG(journal_.warn()) << "loadOutgoing budget-blocked account=" << accountID
                                  << " lines=" << (entry.lines ? entry.lines->size() : 0)
                                  << " total=" << totalLineCount_.load(std::memory_order_relaxed);
        }
        else
        {
            JLOG(journal_.debug())
                << "loadOutgoing chunked account=" << accountID
                << " lines=" << (entry.lines ? entry.lines->size() : 0) << " complete=false";
        }
    }

    auto [ins, ok] = lines_.emplace(accountID, std::move(entry));
    (void)ok;

    JLOG(journal_.trace()) << "loadOutgoingUnlocked ledger " << curSeq << " account " << accountID
                           << " lines=" << ins->second.storedLineCount()
                           << " complete=" << ins->second.cursor.complete
                           << " pins=" << ins->second.pinCount
                           << " total=" << totalLineCount_.load(std::memory_order_relaxed);

    return ins->second.lines;
}

std::shared_ptr<std::vector<PathFindTrustLine>>
AssetCache::getOrLoadOutgoing(AccountID const& accountID)
{
    {
        std::shared_lock const sl(lock_);
        auto const curSeq = ledger_->header().seq;
        auto it = lines_.find(accountID);
        if (it != lines_.end())
        {
            auto const age =
                curSeq >= it->second.loadedSeq ? curSeq - it->second.loadedSeq : curSeq;
            // Fast path only when there is nothing pending to publish.
            if (age <= cacheReuseLedgers_ && it->second.pending.empty())
            {
                ++cacheHits_;
                return it->second.lines;
            }
            if (age > cacheReuseLedgers_)
            {
                // Stale — fall through to unique lock reload.
            }
            else
            {
                // Fresh but has pending chunks — need unique lock to coalesce.
            }
        }
    }

    std::unique_lock const sl(lock_);
    auto const curSeq = ledger_->header().seq;
    auto it = lines_.find(accountID);
    if (it != lines_.end())
    {
        auto const age = curSeq >= it->second.loadedSeq ? curSeq - it->second.loadedSeq : curSeq;
        if (age <= cacheReuseLedgers_)
        {
            ++cacheHits_;
            coalescePendingUnlocked(it->second);
            return it->second.lines;
        }
    }
    return loadOutgoingUnlocked(accountID);
}

std::shared_ptr<std::vector<PathFindTrustLine>>
AssetCache::getRippleLines(AccountID const& accountID)
{
    auto const full = getOrLoadOutgoing(accountID);

    // Pin to the active path_find session (if any) so this account is only
    // freed when that session ends — not when some other session closes.
    //
    // Hot path (already pinned): shared_lock membership check only. Pathfinder
    // calls getRippleLines per hop under SessionPin; an unconditional unique_lock
    // here would serialize kPathSteadyUpdateParallelism workers on every hop.
    // Escalate to unique only on the first pin of this account for the session.
    if (tlsPinSessionId != 0)
    {
        bool alreadyPinned = false;
        {
            std::shared_lock const sl(lock_);
            auto const sit = sessionAccounts_.find(tlsPinSessionId);
            if (sit != sessionAccounts_.end() && sit->second.count(accountID) != 0)
                alreadyPinned = true;
        }
        if (!alreadyPinned)
        {
            std::unique_lock const sl(lock_);
            pinAccountUnlocked(tlsPinSessionId, accountID);
        }
    }

    if (!full || full->empty())
        return nullptr;
    return full;
}

bool
AssetCache::expandIncompleteLines()
{
    std::unique_lock const sl(lock_);
    bool grew = false;
    for (auto& [accountID, entry] : lines_)
    {
        if (entry.cursor.complete)
            continue;
        if (remainingBudgetUnlocked() == 0)
            break;
        if (expandAccountUnlocked(accountID, entry) > 0)
            grew = true;
    }
    return grew;
}

bool
AssetCache::expandIncompleteLinesForSession(int sessionId)
{
    std::unique_lock const sl(lock_);
    auto sit = sessionAccounts_.find(sessionId);
    if (sit == sessionAccounts_.end())
        return false;

    bool grew = false;
    for (auto const& accountID : sit->second)
    {
        auto it = lines_.find(accountID);
        if (it == lines_.end() || it->second.cursor.complete)
            continue;
        if (remainingBudgetUnlocked() == 0)
            break;
        if (expandAccountUnlocked(accountID, it->second) > 0)
            grew = true;
    }
    return grew;
}

bool
AssetCache::hasIncompleteLines() const
{
    std::shared_lock const sl(lock_);
    for (auto const& [_, entry] : lines_)
    {
        if (!entry.cursor.complete)
            return true;
    }
    return false;
}

bool
AssetCache::hasIncompleteLinesForSession(int sessionId) const
{
    std::shared_lock const sl(lock_);
    auto sit = sessionAccounts_.find(sessionId);
    if (sit == sessionAccounts_.end())
        return false;
    for (auto const& accountID : sit->second)
    {
        auto it = lines_.find(accountID);
        if (it != lines_.end() && !it->second.cursor.complete)
            return true;
    }
    return false;
}

std::size_t
AssetCache::releaseSession(int sessionId)
{
    std::unique_lock const sl(lock_);
    auto sit = sessionAccounts_.find(sessionId);
    if (sit == sessionAccounts_.end())
        return 0;

    std::size_t freed = 0;
    for (auto const& accountID : sit->second)
    {
        auto it = lines_.find(accountID);
        if (it == lines_.end())
            continue;

        if (it->second.pinCount > 0)
            --it->second.pinCount;

        if (it->second.pinCount == 0)
        {
            auto const size = it->second.storedLineCount();
            freed += size;
            totalLineCount_.fetch_sub(size, std::memory_order_relaxed);
            lines_.erase(it);
        }
    }
    sessionAccounts_.erase(sit);

    if (freed > 0)
    {
        JLOG(journal_.debug()) << "releaseSession id=" << sessionId << " freed=" << freed
                               << " remaining_lines="
                               << totalLineCount_.load(std::memory_order_relaxed)
                               << " remaining_accounts=" << lines_.size();
    }
    return freed;
}

std::shared_ptr<std::vector<PathFindMPT>>
AssetCache::getMPTs(AccountID const& account)
{
    {
        std::shared_lock const sl(lock_);
        if (auto it = mpts_.find(account); it != mpts_.end())
            return it->second;
    }

    std::unique_lock const sl(lock_);
    if (auto it = mpts_.find(account); it != mpts_.end())
        return it->second;

    std::vector<PathFindMPT> mpts;
    forEachItem(*ledger_, account, [&](SLE::const_ref sle) {
        if (sle->getType() == ltMPTOKEN_ISSUANCE)
        {
            auto const mptID = makeMptID(sle->getFieldU32(sfSequence), account);
            bool const maxedOut = sle->at(sfOutstandingAmount) == maxMPTAmount(*sle);
            mpts.emplace_back(mptID, false, maxedOut);
        }
        else if (sle->getType() == ltMPTOKEN)
        {
            auto const mptID = sle->getFieldH192(sfMPTokenIssuanceID);
            bool const zeroBalance = sle->at(sfMPTAmount) == 0;
            bool const maxedOut = [&] {
                if (auto const sleIssuance = ledger_->read(keylet::mptokenIssuance(mptID)))
                {
                    return sleIssuance->at(sfOutstandingAmount) == maxMPTAmount(*sleIssuance);
                }
                return true;
            }();

            mpts.emplace_back(mptID, zeroBalance, maxedOut);
        }
    });

    if (mpts.empty())
    {
        mpts_.emplace(account, nullptr);
        return nullptr;
    }

    auto inserted = std::make_shared<std::vector<PathFindMPT>>(std::move(mpts));
    mpts_.emplace(account, inserted);
    return inserted;
}

}  // namespace xrpl
