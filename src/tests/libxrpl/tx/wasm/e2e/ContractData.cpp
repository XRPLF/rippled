#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractVmTest.h>

#include <format>
#include <string>

namespace xrpl::test {

// A contract writes a typed value and reads it back, through the real engine and the real
// host over a real ledger.
//
// Two conventions meet here that no other layer can check together. The guest writes a value
// as a type byte followed by a serialization, and reads it back **without** the type byte —
// so a shim that agreed with the host about the bytes but disagreed about the type byte would
// pass `host_calls` and `host_functions` separately and fail only here. The cache is the
// other: the read is answered by a write in the same run, from an account whose data object
// is not on the ledger at all.
struct ContractDataE2e : ContractVmTest
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    // Writes `u16(1234)` under "value_u16", reads it back, and returns the two bytes that
    // came back — big-endian, so `0x04d2` reads as 1234 after the swap the load does.
    [[nodiscard]] std::string
    wat() const
    {
        auto const id = alice.id();
        std::string account;
        for (auto const byte : id)
            account += std::format("\\{:02x}", byte);

        return std::format(
            R"wat(
(module
  (import "host_lib" "set_data_object_field"
    (func $set_data_object_field (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "host_lib" "get_data_object_field"
    (func $get_data_object_field (param i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "value_u16")
  (data (i32.const 48) "\01\04\d2")

  (func (export "escrow_finish") (result i32)
    (local $n i32)

    ;; Store 1234 as an STI_UINT16: the type byte, then the value.
    (local.set $n (call $set_data_object_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 9) (i32.const 48) (i32.const 3)))
    (if (i32.lt_s (local.get $n) (i32.const 0)) (then (return (local.get $n))))

    ;; Read it back. What comes back is the serialization alone, so two bytes, not three.
    (local.set $n (call $get_data_object_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 9) (i32.const 64) (i32.const 32)))
    (if (i32.lt_s (local.get $n) (i32.const 0)) (then (return (local.get $n))))
    (if (i32.ne (local.get $n) (i32.const 2)) (then (return (i32.const -100))))

    (i32.load16_u (i32.const 64))))
)wat",
            account);
    }
};

TEST_F(ContractDataE2e, AValueIsWrittenWithItsTypeAndReadBackWithoutIt)
{
    auto const contractHost =
        makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});

    auto const outcome = run(contractHost, wat());
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);

    // 1234 is 0x04d2. It was stored big-endian, so the little-endian load reads 0xd204.
    EXPECT_EQ(outcome->result, 0xd204);

    auto const& [modified, data] = contractHost.context().result.dataMap.at(alice.id());
    EXPECT_TRUE(modified);
    auto const expected = STJson{STJson::Map{{"value_u16", u16(1234)}}};
    EXPECT_EQ(data.toBlob(), expected.toBlob()) << "what the guest wrote is what the host holds";
}

// The other shape the data calls have, and the one a guest and a host can most easily get
// backwards: a scalar index sitting between two regions. Writing element 1 and reading
// element 1 is not enough — writing 0 and 1 and reading them apart is.
TEST_F(ContractDataE2e, AnIndexBetweenTheRegionsSelectsTheElement)
{
    auto const id = alice.id();
    std::string account;
    for (auto const byte : id)
        account += std::format("\\{:02x}", byte);

    auto const kWat = std::format(
        R"wat(
(module
  (import "host_lib" "set_data_array_element_field"
    (func $set (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "host_lib" "get_data_array_element_field"
    (func $get (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "amount")
  (data (i32.const 48) "\10\0a")
  (data (i32.const 52) "\10\14")

  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $set (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 6)
      (i32.const 0) (i32.const 48) (i32.const 2)))
    (if (i32.lt_s (local.get $n) (i32.const 0)) (then (return (local.get $n))))
    (local.set $n (call $set (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 6)
      (i32.const 1) (i32.const 52) (i32.const 2)))
    (if (i32.lt_s (local.get $n) (i32.const 0)) (then (return (local.get $n))))

    ;; Read element 1, which holds 20. Element 0 holds 10, so a swapped index shows.
    (local.set $n (call $get (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 6)
      (i32.const 1) (i32.const 64) (i32.const 32)))
    (if (i32.lt_s (local.get $n) (i32.const 0)) (then (return (local.get $n))))
    (i32.load8_u (i32.const 64))))
)wat",
        account);

    auto const contractHost =
        makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});

    auto const outcome = run(contractHost, kWat);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, 20) << "element 1, not element 0";
}

}  // namespace xrpl::test
