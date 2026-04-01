#pragma once

#include <boost/predef.h>

#if !BOOST_OS_WINDOWS
#include <sys/resource.h>

#include <dirent.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <optional>

namespace xrpl {

/**
 * FDGuard: File Descriptor monitoring and throttling helper
 *
 * Monitors system file descriptor usage and provides throttling
 * decisions based on configurable thresholds.
 *
 * Thread-safe: All methods are const and stateless.
 */
class FDGuard
{
public:
    struct FDStats
    {
        std::uint64_t used{0};   // Currently open file descriptors
        std::uint64_t limit{0};  // System limit (from getrlimit)
    };

    /**
     * Query current file descriptor usage statistics.
     *
     * @return FDStats if available, std::nullopt on Windows or if query fails
     *
     * Implementation:
     * - POSIX: Uses getrlimit(RLIMIT_NOFILE) for limit,
     *          counts entries in /proc/self/fd (Linux) or /dev/fd (BSD/macOS)
     * - Windows: Always returns std::nullopt
     */
    static std::optional<FDStats>
    query_fd_stats();

    /**
     * Determine if system should throttle based on FD availability.
     *
     * @param free_threshold Minimum ratio of free FDs required (0.0 to 1.0)
     *                       Default: 0.70 (require at least 70% free)
     * @return true if free FDs below threshold (throttle recommended),
     *         false otherwise or if stats unavailable
     *
     * Example: threshold=0.70, limit=1000, used=800
     *   free=200, ratio=0.20 < 0.70 → returns true (throttle)
     */
    static bool
    should_throttle(double free_threshold = 0.70);
};

}  // namespace xrpl
