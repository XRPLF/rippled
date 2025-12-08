#include <xrpl/crypto/csprng.h>

#include <doctest/doctest.h>

using namespace ripple;

TEST_CASE("get values")
{
    auto& engine = crypto_prng();
    auto rand_val = engine();
    CHECK(rand_val >= engine.min());
    CHECK(rand_val <= engine.max());
    uint16_t twoByte{0};
    engine(&twoByte, sizeof(uint16_t));
}
