#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <expected>
#include <format>
#include <string>

namespace xrpl::test {

using testing::Ref;
using testing::Return;

// add_txn_field — a field code the shim translates into an `SField`, plus the bytes that
// field is to be built from.
struct AddTxnFieldCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::format(
            R"wat(
(module
  (import "host_lib" "add_txn_field"
    (func $add_txn_field (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "\40\00\00\00\00\00\00\c0")

  (func (export "escrow_finish") (result i32)
    (call $add_txn_field (i32.const 1) (i32.const {}) (i32.const 0) (i32.const 8)))

  ;; A field code the protocol does not name: a 16-bit integer field nothing declares.
  (func (export "unknown_field") (result i32)
    (call $add_txn_field (i32.const 1) (i32.const {}) (i32.const 0) (i32.const 8)))

  (func (export "negative_index") (result i32)
    (call $add_txn_field (i32.const -1) (i32.const {}) (i32.const 0) (i32.const 8))))
)wat",
            sfAmount.getCode(),
            (static_cast<int>(STI_UINT16) << 16) | 9999,
            sfAmount.getCode());
    }
};

TEST_F(AddTxnFieldCall, TheFieldCodeBecomesAnSFieldAndTheBytesArrive)
{
    auto const amount = Bytes{0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0};
    EXPECT_CALL(host, addTxnField(1U, Ref(sfAmount), SliceIs(amount))).WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0);
}

// A code the protocol does not name has no `SField` to translate to, so the host is never
// asked which field the contract meant.
TEST_F(AddTxnFieldCall, AnUnknownFieldCodeIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, addTxnField).Times(0);

    EXPECT_EQ(hostAnswer("unknown_field"), hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(AddTxnFieldCall, ANegativeIndexIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, addTxnField).Times(0);

    EXPECT_EQ(hostAnswer("negative_index"), hfErrorToInt(HostFunctionError::IndexOutOfBounds));
}

TEST_F(AddTxnFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, addTxnField)
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::FieldNotFound));
}

}  // namespace xrpl::test
