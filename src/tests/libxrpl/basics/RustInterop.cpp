#include <gtest/gtest.h>
#include <rs_hello_world_cxxbridge/lib.h>

#include <string>

TEST(RustInteropTest, hello_world)
{
    EXPECT_EQ(std::string(rs::hello_world::hello_world()), "hello_world");
}
