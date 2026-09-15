#include <xrpl/protocol/STJson.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

#include <string>

namespace xrpl::test {

// emit_event — what a contract says happened, for this node's subscribers. Nothing about an
// event reaches the ledger, so the whole of its effect is the map it lands in.
struct EmitEventImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host()
    {
        return makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});
    }

    static STJson
    event()
    {
        return STJson{STJson::Map{{"uint8", u8(8)}, {"count", u32(32)}}};
    }
};

TEST_F(EmitEventImpl, AnEventIsRecordedUnderItsName)
{
    auto const contractHost = host();

    expectValue(contractHost->emitEvent("transferred", event()), 0);

    auto const& events = contractHost.context().result.eventMap;
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.at("transferred").toBlob(), event().toBlob());
}

TEST_F(EmitEventImpl, EventsWithDifferentNamesAreBothKept)
{
    auto const contractHost = host();

    ASSERT_TRUE(contractHost->emitEvent("first", event()));
    ASSERT_TRUE(contractHost->emitEvent("second", event()));

    EXPECT_EQ(contractHost.context().result.eventMap.size(), 2U);
}

// One name, one event: a contract that emits the same name twice has said one thing twice,
// and the last word is what a subscriber is told.
TEST_F(EmitEventImpl, EmittingTheSameNameTwiceKeepsTheLastEvent)
{
    auto const contractHost = host();
    auto const second = STJson{STJson::Map{{"count", u32(99)}}};

    ASSERT_TRUE(contractHost->emitEvent("transferred", event()));
    ASSERT_TRUE(contractHost->emitEvent("transferred", second));

    auto const& events = contractHost.context().result.eventMap;
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.at("transferred").toBlob(), second.toBlob());
}

}  // namespace xrpl::test
