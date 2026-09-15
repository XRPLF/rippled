#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractVmTest.h>

#include <format>
#include <string>

namespace xrpl::test {

// A contract emits an event it serialized itself.
//
// This is the second wire format a guest writes rather than reads, and it goes through a
// different parser from the one a stored value goes through: a whole `STJson` object, keys
// and all, instead of one typed field. A guest and a host that agreed about values could
// still disagree about this, which is why it is its own case.
struct ContractEventE2e : ContractVmTest
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    static STJson
    event()
    {
        return STJson{STJson::Map{{"count", u32(32)}, {"kind", u8(7)}}};
    }

    [[nodiscard]] std::string
    wat() const
    {
        auto const blob = event().toBlob();
        std::string escaped;
        for (auto const byte : blob)
            escaped += std::format("\\{:02x}", static_cast<std::uint8_t>(byte));

        return std::format(
            R"wat(
(module
  (import "host_lib" "emit_event" (func $emit_event (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "transferred")
  (data (i32.const 32) "{}")

  (func (export "escrow_finish") (result i32)
    (call $emit_event (i32.const 0) (i32.const 11) (i32.const 32) (i32.const {}))))
)wat",
            escaped,
            blob.size());
    }
};

TEST_F(ContractEventE2e, AGuestSerializedEventReachesTheEventMap)
{
    auto const contractHost =
        makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});

    auto const outcome = run(contractHost, wat());
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, 0);

    auto const& events = contractHost.context().result.eventMap;
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.at("transferred").toBlob(), event().toBlob())
        << "what the guest serialized is what the host parsed";
}

}  // namespace xrpl::test
