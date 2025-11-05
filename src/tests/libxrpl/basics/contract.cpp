#include <xrpl/basics/contract.h>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

using namespace ripple;

TEST_CASE("contract")
{
    try
    {
        Throw<std::runtime_error>("Throw test");
    }
    catch (std::runtime_error const& e1)
    {
        CHECK(std::string(e1.what()) == "Throw test");

        try
        {
            Rethrow();
        }
        catch (std::runtime_error const& e2)
        {
            CHECK(std::string(e2.what()) == "Throw test");
        }
        catch (...)
        {
            CHECK(false);
        }
    }
    catch (...)
    {
        CHECK(false);
    }
}
