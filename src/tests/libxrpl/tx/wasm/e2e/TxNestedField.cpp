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
//
// A nested-field getter reaches its leaf through a *locator*: a path of little-endian i32
// steps that the guest lays out in its own memory and the host walks. That is a wire format
// the two sides have to agree on byte for byte, and it is exactly the shape of convention
// that hid the `seq`-as-region bug (see ../README.md) — the kind that a mocked bridge test
// and a direct impl test can both pass while disagreeing with each other, because neither
// one ever has a real guest write the bytes that a real host reads.
//
// Here a real guest writes a two-step locator (`sfMemos`, index 0, `sfMemoData`) and the
// real impl walks it over a real transaction. `host_calls` covers the marshalling and
// `host_functions/TxNestedField.cpp` covers the traversal; this is the one that would catch
// them meaning different things by "a path of i32 steps".
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

    // The locator is three i32 steps the guest stores itself: the array field, the index
    // within it, then the field inside that element. Writing them with `i32.store` rather
    // than a data segment is the point — the guest's own little-endian layout is what the
    // host has to agree with.
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
