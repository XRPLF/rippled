#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// build_txn — one scalar in, one out. The narrowest call in the ABI, and the only one whose
// argument has to be narrowed: a transaction type is sixteen bits inside wasm's thirty-two.
struct BuildTxnCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "build_txn" (func $build_txn (param i32) (result i32)))
  (memory (export "memory") 1)

  (func (export "escrow_finish") (result i32)
    (call $build_txn (i32.const 0)))

  ;; A type that does not fit in sixteen bits, which no transaction type can be.
  (func (export "type_too_large") (result i32)
    (call $build_txn (i32.const 65536)))

  (func (export "negative_type") (result i32)
    (call $build_txn (i32.const -1))))
)wat"};
    }
};

TEST_F(BuildTxnCall, TheTypeReachesTheHostAndTheIndexComesBack)
{
    EXPECT_CALL(host, buildTxn(static_cast<std::uint16_t>(ttPAYMENT))).WillOnce(Return(3));

    EXPECT_EQ(hostAnswer(), 3);
}

// Narrowing an out-of-range type would ask the host to build something else entirely, so it
// is refused instead.
TEST_F(BuildTxnCall, ATypeThatDoesNotFitIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, buildTxn).Times(0);

    EXPECT_EQ(hostAnswer("type_too_large"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(BuildTxnCall, ANegativeTypeIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, buildTxn).Times(0);

    EXPECT_EQ(hostAnswer("negative_type"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(BuildTxnCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, buildTxn)
        .WillOnce(Return(std::unexpected(HostFunctionError::SubmitTxnFailure)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::SubmitTxnFailure));
}

}  // namespace xrpl::test
