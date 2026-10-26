#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace xrpl::test {

// emit_event — what a contract says happened. A successful call's events go into its
// metadata in the order emitted, and subscribers are told of them once the ledger validates.
struct EmitEventImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host()
    {
        return makeTracingContractHost({contract.id(), alice.id()});
    }

    static STJson
    event()
    {
        return STJson{STJson::Map{{"uint8", u8(8)}, {"count", u32(32)}}};
    }
};

TEST_F(EmitEventImpl, AnEventIsRecordedWithItsName)
{
    auto const contractHost = host();

    expectValue(contractHost->emitEvent("transferred", event()), 0);

    auto const& events = contractHost.context().result.events;
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].name, "transferred");
    EXPECT_EQ(events[0].data.toBlob(), event().toBlob());
}

// Every event is kept in the order emitted; a name used twice is two events.
TEST_F(EmitEventImpl, EveryEventIsKeptInTheOrderEmitted)
{
    auto const contractHost = host();
    auto const second = STJson{STJson::Map{{"count", u32(99)}}};

    ASSERT_TRUE(contractHost->emitEvent("transferred", event()));
    ASSERT_TRUE(contractHost->emitEvent("burned", second));
    ASSERT_TRUE(contractHost->emitEvent("transferred", second));

    auto const& events = contractHost.context().result.events;
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].name, "transferred");
    EXPECT_EQ(events[0].data.toBlob(), event().toBlob());
    EXPECT_EQ(events[1].name, "burned");
    EXPECT_EQ(events[2].name, "transferred");
    EXPECT_EQ(events[2].data.toBlob(), second.toBlob());
}

// A call emits at most as many events as it may make writes.
TEST_F(EmitEventImpl, events_past_the_limit_are_refused)
{
    auto const contractHost = host();
    for (std::int64_t i = 0; i < xrpl::contract::maxContractEvents; ++i)
        ASSERT_TRUE(contractHost->emitEvent("tick", event()));

    expectError(contractHost->emitEvent("tick", event()), HostFunctionError::InvalidState);
    EXPECT_NE(logged().find("emitEvent: Exceeded max events"), std::string::npos);
    EXPECT_EQ(
        contractHost.context().result.events.size(),
        static_cast<std::size_t>(xrpl::contract::maxContractEvents));
}

}  // namespace xrpl::test
