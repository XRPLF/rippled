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
 * Test-only: force the process-wide count to 1 (true) or 0 (false).
 */
inline void
setNullBackend(bool value)
{
    nullBackendUsers().store(value ? 1 : 0, std::memory_order_release);
}

/**
 * Shared FullBelowCache is unsafe in null-backend mode.
 */
inline bool
useSharedFullBelowCache()
{
    return !isNullBackend();
}

}  // namespace xrpl
