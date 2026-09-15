#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

#include <cstdint>
#include <memory>

namespace xrpl::test {

// emit_txn — a transaction the contract serialized itself, rather than one it built through
// `build_txn` and `add_txn_field`.
//
// The two paths end in the same place, so what is tested here is the difference: a
// transaction that arrives whole, with whatever the contract chose to put in it.
struct EmitTxnImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract", XRP(2000));
    Account const carol = fund("carol");

    ContractHost
    host()
    {
        return makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});
    }

    // A payment from the contract, assembled the way a contract's own SDK would assemble one.
    std::shared_ptr<STTx const>
    payment(STAmount amount, std::uint32_t sequence, bool innerFlag = true)
    {
        return std::make_shared<STTx const>(ttPAYMENT, [&](STObject& obj) {
            obj.setAccountID(sfAccount, contract.id());
            obj.setAccountID(sfDestination, carol.id());
            obj.setFieldAmount(sfAmount, amount);
            obj.setFieldAmount(sfFee, STAmount{0});
            obj.setFieldU32(sfSequence, sequence);
            obj.setFieldVL(sfSigningPubKey, Blob{});
            if (innerFlag)
                obj.setFieldU32(sfFlags, tfInnerBatchTxn);
        });
    }

    // The contract account's sequence, which an emitted transaction has to carry.
    std::uint32_t
    contractSequence()
    {
        return ledger.getOpenLedger().read(keylet::account(contract.id()))->getFieldU32(sfSequence);
    }
};

TEST_F(EmitTxnImpl, APaymentTheLedgerAcceptsIsQueued)
{
    auto const contractHost = host();

    expectValue(contractHost->emitTxn(payment(XRP(192), contractSequence())), TERtoInt(tesSUCCESS));
    EXPECT_EQ(contractHost.context().result.emittedTxns.size(), 1U);
}

// A transaction the contract did not mark as an inner one cannot be emitted at all.
//
// The host means to set the flag for it, and rebuilds the transaction to do so — but
// rebuilding an `STTx` from its own fields re-runs the format check, which rejects a
// defaulted `sfPaths` that the original carried harmlessly. The contract is told
// `InternalFatal`, so the run stops and the transaction reports `tecINTERNAL`.
//
// This pins what happens today. A contract's own choice reading as a node fault belongs
// with the same fix as `EmitBuiltTxnImpl.ATransactionMissingARequiredFieldStopsTheRun`.
TEST_F(EmitTxnImpl, ATransactionWithoutTheInnerFlagStopsTheRun)
{
    auto const contractHost = host();

    expectError(
        contractHost->emitTxn(payment(XRP(192), contractSequence(), false)),
        HostFunctionError::InternalFatal);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.empty());
}

// One the contract did mark keeps the flag, which is what the transactor reads to know it is
// applying an inner transaction.
TEST_F(EmitTxnImpl, AnEmittedTransactionCarriesTheInnerFlag)
{
    auto const contractHost = host();

    ASSERT_TRUE(contractHost->emitTxn(payment(XRP(192), contractSequence())));

    ASSERT_EQ(contractHost.context().result.emittedTxns.size(), 1U);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.front()->isFlag(tfInnerBatchTxn));
}

// The sequence is the contract's own, and the ledger checks it like any other: a stale one
// is refused with the code that says so.
TEST_F(EmitTxnImpl, AStaleSequenceIsRefused)
{
    auto const contractHost = host();

    auto const result = contractHost->emitTxn(payment(XRP(1), contractSequence() - 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, TERtoInt(tefPAST_SEQ));
    EXPECT_TRUE(contractHost.context().result.emittedTxns.empty());
}

}  // namespace xrpl::test
