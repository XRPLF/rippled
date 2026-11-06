#include <xrpld/app/main/Main.h>

#include <gtest/gtest.h>

namespace xrpl {

// A config file that was read is never fatal.

TEST(MissingConfigIsFatal, read_in_rpc_mode)
{
    EXPECT_FALSE(missingConfigIsFatal(true, true));
}

TEST(MissingConfigIsFatal, read_for_a_server_start)
{
    EXPECT_FALSE(missingConfigIsFatal(true, false));
}

// The search found nothing. The RPC client runs anyway, a server does not.

TEST(MissingConfigIsFatal, search_found_nothing_in_rpc_mode)
{
    EXPECT_FALSE(missingConfigIsFatal(false, true));
}

TEST(MissingConfigIsFatal, search_found_nothing_for_a_server_start)
{
    EXPECT_TRUE(missingConfigIsFatal(false, false));
}

}  // namespace xrpl
