#pragma once

#include <atomic>

namespace xrpl {

/**
 * Thread-safe flag indicating RWDB null-backend mode.
 *
 * Prefer this over XRPL_RWDB_NULL environment variables: POSIX does not
 * require setenv/getenv to be thread-safe, and concurrent use with other
 * library getenv callers is undefined behavior.
 */
inline std::atomic<bool>&
nullBackendFlag()
{
    static std::atomic<bool> flag{false};
    return flag;
}

inline bool
isNullBackend()
{
    return nullBackendFlag().load(std::memory_order_acquire);
}

inline void
setNullBackend(bool value)
{
    nullBackendFlag().store(value, std::memory_order_release);
}

}  // namespace xrpl
