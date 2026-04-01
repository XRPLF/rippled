#include <xrpl/server/FDGuard.h>

namespace xrpl {

std::optional<FDGuard::FDStats>
FDGuard::query_fd_stats()
{
#if BOOST_OS_WINDOWS
    return std::nullopt;
#else
    FDStats s;
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY)
        return std::nullopt;
    s.limit = static_cast<std::uint64_t>(rl.rlim_cur);
#if BOOST_OS_LINUX
    constexpr char const* kFdDir = "/proc/self/fd";
#else
    constexpr char const* kFdDir = "/dev/fd";
#endif
    if (DIR* d = ::opendir(kFdDir))
    {
        std::uint64_t cnt = 0;
        while (::readdir(d) != nullptr)
            ++cnt;
        ::closedir(d);
        // readdir counts '.', '..', and the DIR* itself shows in the list
        s.used = (cnt >= 3) ? (cnt - 3) : 0;
        return s;
    }
    return std::nullopt;
#endif
}

bool
FDGuard::should_throttle(double free_threshold)
{
#if BOOST_OS_WINDOWS
    return false;
#else
    auto const stats = query_fd_stats();
    if (!stats || stats->limit == 0)
        return false;

    auto const& s = *stats;
    auto const free = (s.limit > s.used) ? (s.limit - s.used) : 0ull;
    double const free_ratio = static_cast<double>(free) / static_cast<double>(s.limit);
    if (free_ratio < free_threshold)
    {
        return true;
    }
    return false;
#endif
}

}  // namespace xrpl
