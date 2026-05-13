/** @file
 *  Implementation of `AssetCache`: a per-ledger, thread-safe cache of trust
 *  lines and MPT holdings used by the pathfinding engine.
 *
 *  The cache sits between `Pathfinder` and the ledger SLE store.  Because the
 *  pathfinder may query the same account's trust lines many times during a
 *  single search pass, the cache amortises the cost of repeated SLE reads
 *  across all traversal steps.  It is scoped to a single immutable `ReadView`,
 *  so its contents are always consistent with one ledger version and can never
 *  become stale during the lifetime of a path request.
 *
 *  Key design points documented here:
 *   - Direction-superset optimisation in `getRippleLines()` keeps at most one
 *     trust-line vector per account in memory (always the outgoing superset).
 *   - `nullptr` sentinels in both maps avoid allocating empty vectors for the
 *     estimated >90 % of accounts that have no usable lines for a given search.
 *   - Both `getRippleLines()` and `getMPTs()` hold `lock_` for their entire
 *     operation; concurrency is handled by coarse mutual exclusion.
 */

#include <xrpld/rpc/detail/AssetCache.h>

#include <xrpld/rpc/detail/MPT.h>
#include <xrpld/rpc/detail/TrustLine.h>

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

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace xrpl {

/** Construct the cache for a specific ledger snapshot.
 *
 *  Logs a `debug`-level message recording the ledger sequence so that
 *  cache lifetimes can be correlated with path-search activity.
 *
 *  @param ledger The immutable ledger view this cache is bound to.  Kept
 *      alive by the `shared_ptr` for the lifetime of the cache.
 *  @param j Journal used for diagnostic logging.
 */
AssetCache::AssetCache(std::shared_ptr<ReadView const> const& ledger, beast::Journal j)
    : ledger_(ledger), journal_(j)
{
    JLOG(journal_.debug()) << "created for ledger " << ledger_->header().seq;
}

/** Log cache statistics at destruction.
 *
 *  Emits a `debug`-level message with the final account count and total
 *  distinct trust-line count, giving operators insight into path-search
 *  memory workload for the associated ledger sequence.
 */
AssetCache::~AssetCache()
{
    JLOG(journal_.debug()) << "destroyed for ledger " << ledger_->header().seq << " with "
                           << lines_.size() << " accounts and " << totalLineCount_
                           << " distinct trust lines.";
}

/** Return the cached trust lines for `accountID` under the direction-superset
 *  optimisation.
 *
 *  The outgoing set (all trust lines) is always a superset of the incoming set
 *  (only rippling-enabled lines).  To avoid storing the same data twice the
 *  cache converges to at most one entry per account:
 *
 *   - If the **outgoing** set is requested and an **incoming** subset is already
 *     cached: the smaller subset is erased and the full outgoing set is built
 *     and stored in its place.  `totalLineCount_` is decremented by the erased
 *     count before the new count is added, and an `XRPL_ASSERT` guards against
 *     underflow.
 *   - If the **incoming** set is requested and an **outgoing** superset is
 *     already cached: the superset is returned directly (the key is redirected
 *     to the existing outgoing entry).  Non-rippling lines that it contains are
 *     harmlessly ignored by the pathfinder.
 *   - On the first request for either direction: a `nullptr` sentinel is
 *     emplaced and then replaced by the result of
 *     `PathFindTrustLine::getItems()`.  A second `XRPL_ASSERT` ensures the
 *     emplace only fires when the slot was genuinely unoccupied.
 *
 *  A `nullptr` return value means the account has no usable trust lines for
 *  the given direction; the pathfinder treats this identically to an empty
 *  vector but at lower memory cost.
 *
 *  @param accountID The account whose trust lines are requested.
 *  @param direction `Outgoing` to obtain all lines; `Incoming` to obtain only
 *      rippling-enabled lines (though the superset may be returned if already
 *      cached).
 *  @return A shared pointer to the trust-line vector, or `nullptr` if the
 *      account has no usable lines.  The pointer may safely be shared across
 *      threads; the underlying vector is never mutated after insertion.
 */
std::shared_ptr<std::vector<PathFindTrustLine>>
AssetCache::getRippleLines(AccountID const& accountID, LineDirection direction)
{
    auto const hash = hasher_(accountID);
    AccountKey key(accountID, direction, hash);
    AccountKey otherkey(
        accountID,
        direction == LineDirection::Outgoing ? LineDirection::Incoming : LineDirection::Outgoing,
        hash);

    std::scoped_lock const sl(lock_);

    auto [it, inserted] = [&]() {
        if (auto otheriter = lines_.find(otherkey); otheriter != lines_.end())
        {
            // The whole point of using the direction flag is to reduce the
            // number of trust line objects held in memory. Ensure that there is
            // only a single set of trustlines in the cache per account.
            auto const size = otheriter->second ? otheriter->second->size() : 0;
            JLOG(journal_.info())
                << "Request for "
                << (direction == LineDirection::Outgoing ? "outgoing" : "incoming")
                << " trust lines for account " << accountID << " found " << size
                << (direction == LineDirection::Outgoing ? " incoming" : " outgoing")
                << " trust lines. "
                << (direction == LineDirection::Outgoing ? "Deleting the subset of incoming"
                                                         : "Returning the superset of outgoing")
                << " trust lines. ";
            if (direction == LineDirection::Outgoing)
            {
                // This request is for the outgoing set, but there is already a
                // subset of incoming lines in the cache. Erase that subset
                // to be replaced by the full set. The full set will be built
                // below, and will be returned, if needed, on subsequent calls
                // for either value of outgoing.
                XRPL_ASSERT(
                    size <= totalLineCount_, "xrpl::AssetCache::getRippleLines : maximum lines");
                totalLineCount_ -= size;
                lines_.erase(otheriter);
            }
            else
            {
                // This request is for the incoming set, but there is
                // already a superset of the outgoing trust lines in the cache.
                // The path finding engine will disregard the non-rippling trust
                // lines, so to prevent them from being stored twice, return the
                // outgoing set.
                key = otherkey;
                return std::pair{otheriter, false};
            }
        }
        return lines_.emplace(key, nullptr);
    }();

    if (inserted)
    {
        XRPL_ASSERT(it->second == nullptr, "xrpl::Asset::getRippleLines : null lines");
        auto lines = PathFindTrustLine::getItems(accountID, *ledger_, direction);
        if (!lines.empty())
        {
            it->second = std::make_shared<std::vector<PathFindTrustLine>>(std::move(lines));
            totalLineCount_ += it->second->size();
        }
    }

    XRPL_ASSERT(
        !it->second || !it->second->empty(),
        "xrpl::AssetCache::getRippleLines : null or nonempty lines");
    auto const size = it->second ? it->second->size() : 0;
    JLOG(journal_.trace()) << "getRippleLines for ledger " << ledger_->header().seq << " found "
                           << size
                           << (key.direction == LineDirection::Outgoing ? " outgoing" : " incoming")
                           << " lines for " << (inserted ? "new " : "existing ") << accountID
                           << " out of a total of " << lines_.size() << " accounts and "
                           << totalLineCount_ << " trust lines";

    return it->second;
}

/** Return the cached MPT holdings for `account`, populating on first access.
 *
 *  On the first call for a given account the function scans the account's
 *  owner directory via `forEachItem()` and collects two SLE categories:
 *
 *   - `ltMPTOKEN_ISSUANCE` — the account is the issuer.  `zeroBalance` is
 *     always `false` for issuers; `maxedOut` is `true` when
 *     `sfOutstandingAmount` equals `maxMPTAmount()`.
 *   - `ltMPTOKEN` — the account is a holder.  `zeroBalance` is `true` when
 *     `sfMPTAmount == 0`.  `maxedOut` is derived from the corresponding
 *     issuance SLE; if the issuance SLE cannot be found (e.g. it was deleted),
 *     `maxedOut` defaults conservatively to `true`.
 *
 *  Like `getRippleLines()`, accounts with no MPTs are stored as `nullptr`
 *  rather than an empty vector to reduce allocations for the common case.
 *
 *  @param account The account whose MPT holdings and issuances are requested.
 *  @return A const reference to the internal `shared_ptr` holding the MPT
 *      vector, or a reference to a `nullptr` if the account has no MPTs.
 *      Returning by `const&` avoids an unnecessary reference-count increment
 *      on the hot path.
 *  @note The returned reference is valid only while `lock_` is not re-acquired
 *      by another thread that could rehash `mpts_`; callers must copy the
 *      `shared_ptr` if they need to hold it past the current call frame.
 */
std::shared_ptr<std::vector<PathFindMPT>> const&
AssetCache::getMPTs(xrpl::AccountID const& account)
{
    std::scoped_lock const sl(lock_);

    if (auto it = mpts_.find(account); it != mpts_.end())
        return it->second;

    std::vector<PathFindMPT> mpts;
    forEachItem(*ledger_, account, [&](std::shared_ptr<SLE const> const& sle) {
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
                if (auto const sleIssuance = ledger_->read(keylet::mptIssuance(mptID)))
                {
                    return sleIssuance->at(sfOutstandingAmount) == maxMPTAmount(*sleIssuance);
                }
                // Conservative default: treat missing issuance as maxed out so
                // the pathfinder does not attempt to route through a defunct MPT.
                return true;
            }();

            mpts.emplace_back(mptID, zeroBalance, maxedOut);
        }
    });

    if (mpts.empty())
    {
        mpts_.emplace(account, nullptr);
    }
    else
    {
        mpts_.emplace(account, std::make_shared<std::vector<PathFindMPT>>(std::move(mpts)));
    }

    return mpts_[account];
}

}  // namespace xrpl
