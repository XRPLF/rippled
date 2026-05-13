#include <gtest/gtest.h>

#include <exception>

namespace xrpl::test {

TEST(DISABLED_DetectCrash, detect_crash)
{
    // Kill the process. This is used to test crash reporting manually.
    std::terminate();
}

}  // namespace xrpl::test
