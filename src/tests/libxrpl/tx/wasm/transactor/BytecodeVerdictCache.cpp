#include <xrpl/basics/Slice.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TestServiceRegistry.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/EscrowWasm.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <memory>

namespace xrpl::test {
namespace {

// `EscrowCreate::preclaim` caches the bytecode verdict in the hash router under the transaction
// ID. Preclaim is called directly, without applying, so the same transaction can go through it
// more than once, as it does on open-ledger retries and consensus rebuilds.
struct BytecodeVerdictCache : testing::Test
{
    Account const alice{"alice"};
    Account const carol{"carol"};
    TxTest env;

    BytecodeVerdictCache()
    {
        createAccounts(env, XRP(5'000), alice, carol);
    }

    std::shared_ptr<STTx const>
    escrowCreate(Bytes const& bytecode)
    {
        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
        builder.setBytecode(makeSlice(bytecode));
        builder.setCancelAfter(closeTimeOffset(env, 100));
        builder.setSequence(env.getAccountRoot(alice).getSequence());
        builder.setFee(escrowCreateFee(env, bytecode));
        return builder.build(alice.pk(), alice.sk()).getSTTx();
    }

    TER
    preclaim(STTx const& tx)
    {
        auto& registry = env.getServiceRegistry();
        auto const preflightResult =
            xrpl::preflight(registry, env.getRules(), tx, TapNone, registry.getJournal("apply"));
        return xrpl::preclaim(preflightResult, registry, env.getOpenLedger()).ter;
    }

    HashRouter&
    router()
    {
        return env.getServiceRegistry().getHashRouter();
    }
};

TEST_F(BytecodeVerdictCache, invalid_bytecode_is_refused_on_every_preclaim)
{
    auto const tx = escrowCreate(assembleWat(kImportsUnknownHostFunction));

    EXPECT_EQ(preclaim(*tx), temINVALID_BYTECODE);
    EXPECT_EQ(preclaim(*tx), temINVALID_BYTECODE);
}

TEST_F(BytecodeVerdictCache, valid_bytecode_is_accepted_on_every_preclaim)
{
    auto const tx = escrowCreate(assembleWat(kReadsLedgerSqn));

    EXPECT_EQ(preclaim(*tx), tesSUCCESS);
    EXPECT_EQ(preclaim(*tx), tesSUCCESS);
}

// Each case copies the verdict recorded for one transaction onto a transaction whose bytecode
// says the opposite. Preclaim following the copied verdict shows the module was not compiled
// again, without the test naming which router bits hold the verdict.
TEST_F(BytecodeVerdictCache, a_recorded_invalid_verdict_is_reused)
{
    auto const invalid = escrowCreate(assembleWat(kImportsUnknownHostFunction));
    auto const valid = escrowCreate(assembleWat(kReadsLedgerSqn));

    ASSERT_EQ(preclaim(*invalid), temINVALID_BYTECODE);
    router().setFlags(valid->getTransactionID(), router().getFlags(invalid->getTransactionID()));

    EXPECT_EQ(preclaim(*valid), temINVALID_BYTECODE);
}

TEST_F(BytecodeVerdictCache, a_recorded_valid_verdict_is_reused)
{
    auto const valid = escrowCreate(assembleWat(kReadsLedgerSqn));
    auto const invalid = escrowCreate(assembleWat(kImportsUnknownHostFunction));

    ASSERT_EQ(preclaim(*valid), tesSUCCESS);
    router().setFlags(invalid->getTransactionID(), router().getFlags(valid->getTransactionID()));

    EXPECT_EQ(preclaim(*invalid), tesSUCCESS);
}

}  // namespace
}  // namespace xrpl::test
