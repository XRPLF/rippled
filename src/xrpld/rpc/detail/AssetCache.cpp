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

AssetCache::AssetCache(std::shared_ptr<ReadView const> const& ledger, beast::Journal j)
    : ledger_(ledger), journal_(j)
{
    JLOG(journal_.debug()) << "created for ledger " << ledger_->header().seq;
}

AssetCache::~AssetCache()
{
    JLOG(journal_.debug()) << "destroyed for ledger " << ledger_->header().seq << " with "
                           << lines_.size() << " accounts and " << totalLineCount_
                           << " distinct trust lines.";
}

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

std::shared_ptr<std::vector<PathFindMPT>> const&
AssetCache::getMPTs(xrpl::AccountID const& account)
{
    std::scoped_lock const sl(lock_);

    if (auto it = mpts_.find(account); it != mpts_.end())
        return it->second;

    std::vector<PathFindMPT> mpts;
    // Get issued/authorized tokens
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
                if (auto const sleIssuance = ledger_->read(keylet::mptIssuance(mptID)))
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
    }
    else
    {
        mpts_.emplace(account, std::make_shared<std::vector<PathFindMPT>>(std::move(mpts)));
    }

    return mpts_[account];
}

}  // namespace xrpl
