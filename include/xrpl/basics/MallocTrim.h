#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <chrono>
#include <cstdint>
#include <string_view>

namespace xrpl {

// cSpell:ignore ptmalloc

// -----------------------------------------------------------------------------
// Allocator interaction note:
// - This facility invokes glibc's malloc_trim(0) on Linux/glibc to request that
//   ptmalloc return free heap pages to the OS.
// - If an alternative allocator (e.g. jemalloc or tcmalloc) is linked or
//   preloaded (LD_PRELOAD), calling glibc's malloc_trim typically has no effect
//   on the *active* heap. The call is harmless but may not reclaim memory
//   because those allocators manage their own arenas.
// - Only glibc sbrk/arena space is eligible for trimming; large mmap-backed
//   allocations are usually returned to the OS on free regardless of trimming.
// - Call at known reclamation points (e.g., after cache sweeps / online delete)
//   and consider rate limiting to avoid churn.
// -----------------------------------------------------------------------------

struct MallocTrimReport
{
    bool supported{false};
    int trimResult{-1};
    std::int64_t rssBeforeKB{-1};
    std::int64_t rssAfterKB{-1};
    std::chrono::microseconds durationUs{-1};
    std::int64_t minfltDelta{-1};
    std::int64_t majfltDelta{-1};

    [[nodiscard]] std::int64_t
    deltaKB() const noexcept
    {
        if (rssBeforeKB < 0 || rssAfterKB < 0)
            return 0;
        return rssAfterKB - rssBeforeKB;
    }
};

/**
 * @brief Attempt to return freed memory to the operating system.
 *
 * On Linux with glibc malloc, this issues ::malloc_trim(0), which may release
 * free space from ptmalloc arenas back to the kernel. On other platforms, or if
 * a different allocator is in use, this function is a no-op and the report will
 * indicate that trimming is unsupported or had no effect.
 *
 * @param tag     Identifier for logging/debugging purposes.
 * @param journal Journal for diagnostic logging.
 * @return Report containing before/after metrics and the trim result.
 *
 * @note If an alternative allocator (jemalloc/tcmalloc) is linked or preloaded,
 *       calling glibc's malloc_trim may have no effect on the active heap. The
 *       call is harmless but typically does not reclaim memory under those
 *       allocators.
 *
 * @note Only memory served from glibc's sbrk/arena heaps is eligible for trim.
 *       Large allocations satisfied via mmap are usually returned on free
 *       independently of trimming.
 *
 * @note Intended for use after operations that free significant memory (e.g.,
 *       cache sweeps, ledger cleanup, online delete). Consider rate limiting.
 */
MallocTrimReport
mallocTrim(std::string_view tag, beast::Journal journal);

}  // namespace xrpl
