#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>

namespace xrpl::test {

struct UpdateDataImpl : WasmImplTest
{
};

TEST_F(UpdateDataImpl, SmallData)
{
    auto& h = host();
    auto data = Bytes(10, 0x42);
    auto result = h.updateData(Slice{data.data(), data.size()});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, data.size());
    // TODO: getData() does not seem to be called when the smart escrow finishes.
    EXPECT_EQ(h.getData(), data);
}

TEST_F(UpdateDataImpl, LargeData)
{
    auto& h = host();
    auto data = Bytes(kMaxWasmDataLength + 1, 0x42);
    auto result = h.updateData(Slice{data.data(), data.size()});
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::DataFieldTooLarge);
}

}  // namespace xrpl::test
