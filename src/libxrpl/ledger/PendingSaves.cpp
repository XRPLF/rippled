#include <xrpl/ledger/PendingSaves.h>

#include <xrpl/protocol/Protocol.h>

#include <map>
#include <mutex>

namespace xrpl {

bool
PendingSaves::startWork(LedgerIndex seq)
{
    std::scoped_lock const lock(mutex_);

    auto const it = map_.find(seq);

    if ((it == map_.end()) || it->second)
    {
        // Work done or another thread is doing it
        return false;
    }

    it->second = true;
    return true;
}

void
PendingSaves::finishWork(LedgerIndex seq)
{
    std::scoped_lock const lock(mutex_);

    map_.erase(seq);
    await_.notify_all();
}

bool
PendingSaves::pending(LedgerIndex seq)
{
    std::scoped_lock const lock(mutex_);
    return map_.contains(seq);
}

bool
PendingSaves::shouldWork(LedgerIndex seq)
{
    std::scoped_lock const lock(mutex_);
    return map_.try_emplace(seq, false).second;
}

void
PendingSaves::claimWork(LedgerIndex seq)
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (true)
    {
        auto const [it, inserted] = map_.try_emplace(seq, true);
        if (inserted || !it->second)
        {
            // Nobody was saving it, or it was scheduled but not dispatched.
            it->second = true;
            return;
        }

        // Already in progress, wait for that save to finish.
        await_.wait(lock);
    }
}

std::map<LedgerIndex, bool>
PendingSaves::getSnapshot() const
{
    std::scoped_lock const lock(mutex_);

    return map_;
}

}  // namespace xrpl
