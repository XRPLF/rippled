#include <xrpl/nodestore/Backend.h>

#include <algorithm>
#include <thread>

namespace xrpl {
namespace NodeStore {

// Initialize the static constant for hardware thread count. The `hardware_concurrency` function can
// return 0 on some platforms, in which case we default to 1. We limit the total number of threads
// to 8 to avoid contention.
unsigned int const Backend::numHardwareThreads = []() {
    auto const hw = std::thread::hardware_concurrency();
    return std::min(std::max(hw, 1u), 8u);
}();

std::pair<unsigned int, unsigned int>
Backend::calculateBatchParallelism(unsigned int batchSize, unsigned int maxThreadCount)
{
    // Estimate the number of threads using ceiling division: aim for at least 4 items per thread,
    // but don't exceed the number of available threads.
    auto const initialThreads = std::min((batchSize + 3u) / 4u, maxThreadCount);

    // Calculate number of items per thread.
    auto const numItems = (batchSize + initialThreads - 1u) / initialThreads;

    // Calculate the actual number of threads needed. After rounding up numItems, we may need fewer
    // threads than initially estimated.
    auto const actualThreads = (batchSize + numItems - 1u) / numItems;

    // Sanity checks.
    XRPL_ASSERT(
        numItems <= batchSize,
        "xrpl::NodeStore::Backend::calculateBatchParallelism : numItems <= batchSize");
    XRPL_ASSERT(
        actualThreads <= batchSize,
        "xrpl::NodeStore::Backend::calculateBatchParallelism : actualThreads <= batchSize");
    XRPL_ASSERT(
        actualThreads <= maxThreadCount,
        "xrpl::NodeStore::Backend::calculateBatchParallelism : actualThreads <= hwThreadCount");
    if (numItems > batchSize || actualThreads > batchSize || actualThreads > maxThreadCount)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::NodeStore::Backend::calculateBatchParallelism : sanity check failed");
        return {1, batchSize};
        // LCOV_EXCL_STOP
    }

    return {actualThreads, numItems};
}

}  // namespace NodeStore
}  // namespace xrpl
