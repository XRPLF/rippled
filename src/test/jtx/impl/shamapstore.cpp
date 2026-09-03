#include <test/jtx/shamapstore.h>

#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/SHAMapStore.h>

#include <xrpl/core/JobQueue.h>

#include <chrono>
#include <optional>
#include <ostream>

namespace xrpl::test::jtx {

bool
syncStore(Env& env, std::chrono::milliseconds const timeout)
{
    // Drain the job queue first, so that onLedgerClosed() has run and working_
    // is set. Then wait for the store itself, so that a store which never
    // finishes fails the caller's test instead of blocking on it.
    //
    // Both waits are bounded, and the same bound serves both: a caller that
    // reaches the second one has already spent whatever the first one took, but
    // the total is only relevant when something is broken, and then the exact
    // total does not matter.
    return env.app().getJobQueue().rendezvous(timeout) &&
        env.app().getSHAMapStore().rendezvous(timeout);
}

std::optional<int>
initializeStore(Env& env, int const maxExtraCloses)
{
    auto& store = env.app().getSHAMapStore();

    for (int extraCloses = 0;; ++extraCloses)
    {
        if (!syncStore(env))
            return std::nullopt;
        if (store.getLastRotated() != 0 || extraCloses == maxExtraCloses)
        {
            if (extraCloses != 0)
            {
                env.test.log << "initializeStore: the store needed " << extraCloses
                             << " extra ledger close(s) to pick up a validated ledger. "
                                "SHAMapStoreImp::run() dropped the notification for the "
                                "first one; see the comment on initializeStore()."
                             << std::endl;
            }
            return extraCloses;
        }
        env.close();
    }
}

}  // namespace xrpl::test::jtx
