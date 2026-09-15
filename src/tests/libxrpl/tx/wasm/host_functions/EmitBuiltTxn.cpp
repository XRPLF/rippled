#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

#include <cstdint>

namespace xrpl::test {

// emit_built_txn — applying a transaction the contract assembled.
//
// The answer is a TER rather than a `HostFunctionError`: a transaction the ledger refuses is
// something the contract is told about and may act on, not a fault in the call.
struct EmitBuiltTxnImpl : ContractHostFixture
{
    Account const alice = fund("alice");

    // The contract's account pays what it emits, so it is funded well above its reserve.
    Account const contract = fund("contract", XRP(2000));
    Account const carol = fund("carol");

    ContractHost
    host()
    {
        return makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});
    }

    // A payment of `amount` from the contract to carol, built through the host as a contract
    // would build it.
    static void
    buildPayment(ContractHost const& contractHost, AccountID const& destination, STAmount amount)
    {
        auto const amountBytes = WasmLedger::toBytes(amount);
        auto const destinationBytes = accountField(destination);
        ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));
        ASSERT_TRUE(
            contractHost->addTxnField(0, sfAmount, Slice{amountBytes.data(), amountBytes.size()}));
        ASSERT_TRUE(contractHost->addTxnField(
            0, sfDestination, Slice{destinationBytes.data(), destinationBytes.size()}));
    }
};

TEST_F(EmitBuiltTxnImpl, APaymentTheLedgerAcceptsIsQueuedAndPaid)
{
    auto const contractHost = host();
    buildPayment(contractHost, carol.id(), XRP(192));

    expectValue(contractHost->emitBuiltTxn(0), TERtoInt(tesSUCCESS));
    EXPECT_EQ(contractHost.context().result.emittedTxns.size(), 1U)
        << "an accepted transaction is queued for the transactor to apply";
}

// The emitted transaction is applied against a view the contract's later calls see, so a
// second emit is priced against the balance the first one left.
TEST_F(EmitBuiltTxnImpl, ASecondEmitSeesWhatTheFirstSpent)
{
    auto const contractHost = host();
    buildPayment(contractHost, carol.id(), XRP(192));
    ASSERT_TRUE(contractHost->emitBuiltTxn(0));

    auto const emitted = contractHost.context().emitView->read(keylet::account(contract.id()));
    ASSERT_NE(emitted, nullptr);
    EXPECT_LT(emitted->getFieldAmount(sfBalance), STAmount{XRP(2000)});
}

// A payment the contract cannot afford still reaches the ledger: a `tec` claims a fee, so it
// is a real transaction and is queued like any other. What the contract gets is the code,
// which is how it learns the payment did not happen.
//
// This is the code that motivated writing the TER to a buffer rather than returning it: it is
// negative, and a negative return is read as a host error.
TEST_F(EmitBuiltTxnImpl, APaymentTheAccountCannotAffordIsReportedAsItsTec)
{
    auto const contractHost = host();
    buildPayment(contractHost, carol.id(), XRP(1'000'000));

    expectValue(contractHost->emitBuiltTxn(0), TERtoInt(tecUNFUNDED_PAYMENT));
    EXPECT_EQ(contractHost.context().result.emittedTxns.size(), 1U)
        << "a fee-claiming transaction belongs on the ledger";
}

// A transaction missing a field its type requires never becomes a transaction, and the
// contract is told so: `SubmitTxnFailure` is a refusal it can act on, where the run-stopping
// `InternalFatal` would have reported a node fault for a mistake of its own.
TEST_F(EmitBuiltTxnImpl, ATransactionMissingARequiredFieldIsRefused)
{
    auto const contractHost = host();
    auto const amount = WasmLedger::toBytes(STAmount{XRP(1)});
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));
    ASSERT_TRUE(contractHost->addTxnField(0, sfAmount, Slice{amount.data(), amount.size()}));

    // No destination, so the payment cannot be built as one.
    expectError(contractHost->emitBuiltTxn(0), HostFunctionError::SubmitTxnFailure);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.empty());
}

// A refusal leaves the transaction where it was, so the contract can supply what was missing
// and emit the same index again.
TEST_F(EmitBuiltTxnImpl, ARefusedTransactionCanBeCorrectedAndEmitted)
{
    auto const contractHost = host();
    auto const amount = WasmLedger::toBytes(STAmount{XRP(192)});
    auto const destination = accountField(carol.id());
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));
    ASSERT_TRUE(contractHost->addTxnField(0, sfAmount, Slice{amount.data(), amount.size()}));
    ASSERT_FALSE(contractHost->emitBuiltTxn(0));

    ASSERT_TRUE(
        contractHost->addTxnField(0, sfDestination, Slice{destination.data(), destination.size()}));
    expectValue(contractHost->emitBuiltTxn(0), TERtoInt(tesSUCCESS));
}

TEST_F(EmitBuiltTxnImpl, AnIndexNamingNoTransactionIsOutOfBounds)
{
    expectError(host()->emitBuiltTxn(0), HostFunctionError::IndexOutOfBounds);
}

}  // namespace xrpl::test
