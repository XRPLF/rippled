#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/protocol_autogen/transactions/EscrowFinish.h>
#include <xrpl/protocol_autogen/transactions/Payment.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/EscrowWasm.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>
#include <string_view>

namespace xrpl::test {
namespace {

// `WasmScope` is how `simulate` decides whether to charge the heavier RPC rate. "Was the
// engine entered" is broader than anything the metadata records, so these tests pin the
// outcomes where the two disagree.

constexpr std::uint32_t kAllowance = 10'000;

struct VmScope : testing::Test
{
    TxTest env;
    Account const alice{"alice"};
    Account const carol{"carol"};

    VmScope()
    {
        createAccounts(env, XRP(5'000), alice, carol);
    }

    std::uint32_t
    createEscrow(std::string_view wat)
    {
        auto const wasm = assembleWat(wat);
        auto const seq = env.getAccountRoot(alice).getSequence();

        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
        builder.setBytecode(makeSlice(wasm));
        builder.setCancelAfter(closeTimeOffset(env, 1'000));

        EXPECT_EQ(env.submit(builder, alice, escrowCreateFee(env, wasm)).ter, tesSUCCESS);
        env.close();
        return seq;
    }

    // Finishes `seq` inside a scope and reports both halves of the answer.
    struct Finished
    {
        TER ter;
        bool entered;
    };

    [[nodiscard]] Finished
    finish(std::uint32_t seq)
    {
        auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
        builder.setGas(kAllowance);

        auto const wasm = WasmScope{};
        auto const ter = env.submit(builder, carol, escrowFinishFee(env, kAllowance)).ter;
        return Finished{.ter = ter, .entered = wasm.entered()};
    }
};

// The baseline, and the only case the metadata could also have answered.
TEST_F(VmScope, a_completed_run_counts)
{
    auto const result = finish(createEscrow(kReadsLedgerSqn));

    EXPECT_EQ(result.ter, tesSUCCESS);
    EXPECT_TRUE(result.entered);
}

// The case that motivated the scope: the most expensive thing a simulation can ask for is
// also the one the metadata says least about. A contract that loops forever burns the
// whole allowance and reports no return code at all.
TEST_F(VmScope, running_out_of_gas_counts_though_no_return_code_is_recorded)
{
    auto const result = finish(createEscrow(kLoopsForever));

    EXPECT_EQ(result.ter, tecOUT_OF_GAS);
    EXPECT_TRUE(result.entered);
}

// A fault, not a rejection: gas is recorded, a return code is not.
TEST_F(VmScope, a_trap_counts)
{
    auto const result = finish(createEscrow(kTraps));

    EXPECT_EQ(result.ter, tecFAILED_PROCESSING);
    EXPECT_TRUE(result.entered);
}

// Bytecode screening happens in preclaim and compiles the module before it can refuse it.
// That work is on the way to a `tem`, which claims no fee and so builds no metadata at
// all — there is no field such an answer could have been written to.
TEST_F(VmScope, screening_rejected_bytecode_counts)
{
    auto const wasm = assembleWat(kImportsUnknownHostFunction);

    auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
    builder.setBytecode(makeSlice(wasm));
    builder.setCancelAfter(closeTimeOffset(env, 1'000));

    auto const scope = WasmScope{};
    auto const result = env.submit(builder, alice, escrowCreateFee(env, wasm));

    EXPECT_EQ(result.ter, temINVALID_BYTECODE);
    EXPECT_TRUE(scope.entered()) << "a module that fails screening was still compiled";
    EXPECT_FALSE(result.metadata.has_value()) << "so no metadata field could have carried it";
}

TEST_F(VmScope, screening_accepted_bytecode_counts)
{
    auto const wasm = assembleWat(kReadsLedgerSqn);

    auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
    builder.setBytecode(makeSlice(wasm));
    builder.setCancelAfter(closeTimeOffset(env, 1'000));

    auto const scope = WasmScope{};
    EXPECT_EQ(env.submit(builder, alice, escrowCreateFee(env, wasm)).ter, tesSUCCESS);
    EXPECT_TRUE(scope.entered());
}

// The other direction: something that never goes near the engine must not count, or the
// heavier rate stops meaning anything.
TEST_F(VmScope, a_transaction_that_never_runs_wasm_does_not_count)
{
    auto const scope = WasmScope{};
    auto const result =
        env.submit(transactions::PaymentBuilder{alice, carol, STAmount{XRP(1)}}, alice);

    EXPECT_EQ(result.ter, tesSUCCESS);
    EXPECT_FALSE(scope.entered());
}

TEST_F(VmScope, an_escrow_without_bytecode_does_not_count)
{
    auto const seq = env.getAccountRoot(alice).getSequence();

    auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
    builder.setFinishAfter(closeTimeOffset(env, 1));
    ASSERT_EQ(env.submit(builder, alice).ter, tesSUCCESS);
    env.close();
    env.close();

    auto const scope = WasmScope{};
    auto const result = env.submit(transactions::EscrowFinishBuilder{carol, alice, seq}, carol);

    EXPECT_EQ(result.ter, tesSUCCESS);
    EXPECT_FALSE(scope.entered());
}

// Scopes nest and do not disturb each other, which is why the count is a count rather
// than a flag. A scope opened after the work is done reports nothing.
TEST_F(VmScope, scopes_measure_their_own_span)
{
    auto const seq = createEscrow(kReadsLedgerSqn);

    auto const outer = WasmScope{};
    {
        auto const inner = WasmScope{};
        ASSERT_EQ(finish(seq).ter, tesSUCCESS);
        EXPECT_TRUE(inner.entered());
    }
    EXPECT_TRUE(outer.entered()) << "the inner scope must not consume the outer's answer";

    auto const after = WasmScope{};
    EXPECT_FALSE(after.entered());
}

}  // namespace
}  // namespace xrpl::test
