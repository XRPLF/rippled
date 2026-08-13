#pragma once

#include <atomic>

namespace xrpl {

/**
 * Thread-safe refcount for RWDB null-backend mode.
 *
 * libxrpl helpers cannot reach Config, so SHAMapStoreImp publishes the
 * mode here. A refcount is used so overlapping Application / SHAMapStoreImp
 * lifetimes (common in unit tests) do not clear the flag while another
 * instance still needs it.
 *
 * Prefer this over XRPL_RWDB_NULL environment variables: POSIX does not
 * require setenv/getenv to be thread-safe, and concurrent use with other
 * library getenv callers is undefined behavior.
 */
inline std::atomic<int>&
nullBackendUsers()
{
    static std::atomic<int> users{0};
    return users;
}

inline bool
isNullBackend()
{
    return nullBackendUsers().load(std::memory_order_acquire) > 0;
}

inline void
acquireNullBackend()
{
    nullBackendUsers().fetch_add(1, std::memory_order_acq_rel);
}

inline void
releaseNullBackend()
{
    int prev = nullBackendUsers().fetch_sub(1, std::memory_order_acq_rel);
    if (prev <= 0)
    {
        // Restore a non-negative count if release is unbalanced.
        nullBackendUsers().fetch_add(1, std::memory_order_relaxed);
    }
}

/**
 * Test helper: acquire one extra null-backend claim for the scope.
 * Never stores an absolute 0/1, so it cannot drop another live
 * SHAMapStoreImp out of null mode.
 */
class NullBackendScope
{
    bool const armed_;

public:
    explicit NullBackendScope(bool enable) : armed_(enable)
    {
        if (armed_)
            acquireNullBackend();
    }

    ~NullBackendScope()
    {
        if (armed_)
            releaseNullBackend();
    }

    NullBackendScope(NullBackendScope const&) = delete;
    NullBackendScope&
    operator=(NullBackendScope const&) = delete;
};

/**
 * Shared FullBelowCache is unsafe in null-backend mode.
 */
inline bool
useSharedFullBelowCache()
{
    return !isNullBackend();
}

}  // namespace xrpl
