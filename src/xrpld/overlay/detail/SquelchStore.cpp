#include <xrpld/overlay/SquelchStore.h>

#include <xrpld/overlay/ReduceRelayCommon.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <chrono>
#include <unordered_map>

namespace xrpl::reduce_relay {

void
SquelchStore::handleSquelch(PublicKey const& validator, bool squelch, std::chrono::seconds duration)
{
    // Remove all expired squelches. This call is here, as it is on the least
    // critical execution path, that does not require periodic cleanup calls.
    removeExpired();

    if (squelch)
    {
        // This should never trigger. The squelch duration is validated in
        // PeerImp.onMessage(TMSquelch). However, if somehow invalid duration is
        // passed, log is as an error
        if ((duration < reduce_relay::kMinUnsquelchExpire ||
             duration > reduce_relay::kMaxUnsquelchExpirePeers))
        {
            JLOG(journal_.error())
                << "SquelchStore: invalid squelch duration validator: " << Slice(validator)
                << " duration: " << duration.count();
            return;
        }

        add(validator, duration);
        return;
    }

    remove(validator);
}

bool
SquelchStore::isSquelched(PublicKey const& validator) const
{
    auto const now = clock_.now();

    auto const it = squelched_.find(validator);
    if (it == squelched_.end())
        return false;

    return it->second > now;
}

void
SquelchStore::add(PublicKey const& validator, std::chrono::seconds const& duration)
{
    squelched_[validator] = clock_.now() + duration;
}

void
SquelchStore::remove(PublicKey const& validator)
{
    squelched_.erase(validator);
}

void
SquelchStore::removeExpired()
{
    auto const now = clock_.now();
    std::erase_if(squelched_, [&](auto const& entry) { return entry.second < now; });
}

}  // namespace xrpl::reduce_relay
