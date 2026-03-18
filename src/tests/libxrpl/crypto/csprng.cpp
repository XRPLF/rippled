#include <xrpl/crypto/csprng.h>

#include <gtest/gtest.h>

using namespace xrpl;

TEST(csprng, get_values)
{
    auto& engine = crypto_prng();
    auto rand_val = engine();
    EXPECT_GE(rand_val, engine.min());
    EXPECT_LE(rand_val, engine.max());
    uint16_t twoByte{0};
    engine(&twoByte, sizeof(uint16_t));
}
