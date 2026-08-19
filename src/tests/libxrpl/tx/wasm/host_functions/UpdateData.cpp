#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct UpdateDataImpl : RealHostFixture
{
};

TEST_F(UpdateDataImpl, SmallData)
{
    auto h = makeHost();
    auto data = Bytes(10, 0x42);
    expectValue(h->updateData(Slice{data.data(), data.size()}), data.size());
    // TODO: getData() does not seem to be called when the smart escrow finishes.
    EXPECT_EQ(h->getData(), data);
}

TEST_F(UpdateDataImpl, LargeData)
{
    auto h = makeHost();
    auto data = Bytes(kMaxWasmDataLength + 1, 0x42);
    expectError(
        h->updateData(Slice{data.data(), data.size()}), HostFunctionError::DataFieldTooLarge);
}

}  // namespace xrpl::test
