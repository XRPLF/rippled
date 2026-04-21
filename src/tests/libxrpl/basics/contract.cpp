#include <xrpl/basics/contract.h>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace xrpl;

TEST(contract, contract)
{
    try
    {
        Throw<std::runtime_error>("Throw test");
    }
    catch (std::runtime_error const& e1)
    {
        EXPECT_STREQ(e1.what(), "Throw test");

        try
        {
            Rethrow();
        }
        catch (std::runtime_error const& e2)
        {
            EXPECT_STREQ(e2.what(), "Throw test");
        }
        catch (...)
        {
            FAIL() << "std::runtime_error should have been re-caught";
        }
    }
    catch (...)
    {
        FAIL() << "std::runtime_error should have been caught the first time";
    }
}
