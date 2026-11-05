#include <xrpl/basics/NameRegistry.h>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace xrpl;

TEST(NameRegistry, add_and_find)
{
    NameRegistry registry;
    registry.add("alice");
    registry.add("bob");

    EXPECT_EQ(registry.size(), 2u);
    EXPECT_EQ(registry.find("al"), "alice");
    EXPECT_EQ(registry.find("carol"), "");
    EXPECT_THROW(registry.add(""), std::invalid_argument);
}

TEST(NameRegistry, greeting)
{
    NameRegistry registry;
    registry.add("alice");

    std::string out;
    registry.greeting(0, NameRegistry::Style::Formal, out);
    EXPECT_EQ(out, "Dear alice, you are visitor number 1.");

    registry.greeting(0, NameRegistry::Style::Casual, out);
    EXPECT_EQ(out, "Hi alice!");
}
