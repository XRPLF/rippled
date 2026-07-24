#include <xrpl/crypto/csprng.h>

#include <gtest/gtest.h>

#include <cstdint>

using namespace xrpl;

TEST(csprng, get_values)
{
    auto& engine = cryptoPrng();
    auto randVal = engine();
    EXPECT_GE(randVal, engine.min());
    EXPECT_LE(randVal, engine.max());
    uint16_t twoByte{0};
    engine(&twoByte, sizeof(uint16_t));
}
