#include <xrpld/app/paths/RippleLineCache.h>
#include <xrpld/app/paths/TrustLine.h>

namespace ripple {

RippleLineCache::RippleLineCache(
    std::shared_ptr<ReadView const> const& ledger,
    beast::Journal j)
    : ledger_(ledger), journal_(j)
{
    JLOG(journal_.debug()) << "created for ledger " << ledger_->info().seq;
}

RippleLineCache::~RippleLineCache()
{
    JLOG(journal_.debug()) << "destroyed for ledger " << ledger_->info().seq
                           << " with " << lines_.size() << " accounts and "
                           << totalLineCount_ << " distinct trust lines.";
}

std::shared_ptr<std::vector<PathFindTrustLine>>
RippleLineCache::getRippleLines(
    AccountID const& accountID,
    LineDirection direction)
{
    auto const hash = hasher_(accountID);
    AccountKey key(accountID, direction, hash);
    AccountKey otherkey(
        accountID,
        direction == LineDirection::outgoing ? LineDirection::incoming
                                             : LineDirection::outgoing,
        hash);

    std::lock_guard sl(mLock);

    auto [it, inserted] = [&]() {
        if (auto otheriter = lines_.find(otherkey); otheriter != lines_.end())
        {
            // The whole point of using the direction flag is to reduce the
            // number of trust line objects held in memory. Ensure that there is
            // only a single set of trustlines in the cache per account.
            auto const size = otheriter->second ? otheriter->second->size() : 0;
            JLOG(journal_.info())
                << "Request for "
                << (direction == LineDirection::outgoing ? "outgoing"
                                                         : "incoming")
                << " trust lines for account " << accountID << " found " << size
                << (direction == LineDirection::outgoing ? " incoming"
                                                         : " outgoing")
                << " trust lines. "
                << (direction == LineDirection::outgoing
                        ? "Deleting the subset of incoming"
                        : "Returning the superset of outgoing")
                << " trust lines. ";
            if (direction == LineDirection::outgoing)
            {
                // This request is for the outgoing set, but there is already a
                // subset of incoming lines in the cache. Erase that subset
                // to be replaced by the full set. The full set will be built
                // below, and will be returned, if needed, on subsequent calls
                // for either value of outgoing.
                XRPL_ASSERT(
                    size <= totalLineCount_,
                    "ripple::RippleLineCache::getRippleLines : maximum lines");
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
        XRPL_ASSERT(
            it->second == nullptr,
            "ripple::RippleLineCache::getRippleLines : null lines");
        auto lines =
            PathFindTrustLine::getItems(accountID, *ledger_, direction);
        if (lines.size())
        {
            it->second = std::make_shared<std::vector<PathFindTrustLine>>(
                std::move(lines));
            totalLineCount_ += it->second->size();
        }
    }

    XRPL_ASSERT(
        !it->second || (it->second->size() > 0),
        "ripple::RippleLineCache::getRippleLines : null or nonempty lines");
    auto const size = it->second ? it->second->size() : 0;
    JLOG(journal_.trace()) << "getRippleLines for ledger "
                           << ledger_->info().seq << " found " << size
                           << (key.direction_ == LineDirection::outgoing
                                   ? " outgoing"
                                   : " incoming")
                           << " lines for " << (inserted ? "new " : "existing ")
                           << accountID << " out of a total of "
                           << lines_.size() << " accounts and "
                           << totalLineCount_ << " trust lines";

    return it->second;
}

}  // namespace ripple
