#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/RealVmTest.h>

#include <string>
#include <utility>

namespace xrpl::test {

// The locator convention, end to end.
struct TxNestedFieldE2e : RealVmTest
{
    // An EscrowFinish carrying a memo, so the locator has a real leaf to reach.
    TxAssembler
    withMemo(Account const& acct)
    {
        auto assembler = escrowFinishTx(ledger, acct);
        assembler.build = [inner = std::move(assembler.build)](STObject& obj) {
            inner(obj);
            auto memos = STArray{};
            auto memo = STObject::makeInnerObject(sfMemo);
            memo.setFieldVL(sfMemoData, Slice{"hello", 5});
            memos.push_back(std::move(memo));
            obj.setFieldArray(sfMemos, memos);
        };
        return assembler;
    }
};

TEST_F(TxNestedFieldE2e, ContractWalksALocatorToANestedTransactionField)
{
    auto const owner = fund("owner");
    auto assembler = withMemo(owner);

    auto const wat = std::string{R"wat(
(module
  (import "host_lib" "tx_inner" (func $tx_inner (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (i32.store (i32.const 0) (i32.const )wat"} +
        std::to_string(sfMemos.getCode()) + R"wat())
    (i32.store (i32.const 4) (i32.const 0))
    (i32.store (i32.const 8) (i32.const )wat" +
        std::to_string(sfMemoData.getCode()) + R"wat())
    (call $tx_inner (i32.const 0) (i32.const 12) (i32.const 64) (i32.const 32))))
)wat";

    auto const outcome = run(wat, keylet::account(owner.id()), assembler.type, assembler.build);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    // Five bytes: "hello", the memo's data, reached through the locator.
    EXPECT_EQ(outcome->result, 5);
}

}  // namespace xrpl::test
