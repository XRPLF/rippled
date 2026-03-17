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

}  // namespace NodeStore
}  // namespace xrpl
