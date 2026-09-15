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

// A contract does not have to mark what it emits as an inner transaction: the host marks it,
// on a copy, and the transaction is applied like any other.
TEST_F(EmitTxnImpl, ATransactionWithoutTheInnerFlagIsMarkedAndEmitted)
{
    auto const contractHost = host();

    expectValue(
        contractHost->emitTxn(payment(XRP(192), contractSequence(), false)), TERtoInt(tesSUCCESS));

    ASSERT_EQ(contractHost.context().result.emittedTxns.size(), 1U);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.front()->isFlag(tfInnerBatchTxn));
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
