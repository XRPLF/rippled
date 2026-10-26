#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>
#include <memory>
#include <utility>

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
        return makeTracingContractHost({contract.id(), alice.id()});
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

// A contract emits only what it may build: a type marked not emittable, such as one that
// gives its pseudo-account a key, is refused whole and changes nothing.
TEST_F(EmitTxnImpl, a_type_a_contract_may_not_emit_is_refused)
{
    auto const contractHost = host();
    auto const rekey = std::make_shared<STTx const>(ttREGULAR_KEY_SET, [&](STObject& obj) {
        obj.setAccountID(sfAccount, contract.id());
        obj.setAccountID(sfRegularKey, carol.id());
        obj.setFieldAmount(sfFee, STAmount{0});
        obj.setFieldU32(sfSequence, contractSequence());
        obj.setFieldVL(sfSigningPubKey, Blob{});
        obj.setFieldU32(sfFlags, tfInnerBatchTxn);
    });

    expectError(contractHost->emitTxn(rekey), HostFunctionError::SubmitTxnFailure);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.empty());
    EXPECT_FALSE(
        ledger.getOpenLedger().read(keylet::account(contract.id()))->isFieldPresent(sfRegularKey));
}

// A transaction from another account is not the contract's to emit.
TEST_F(EmitTxnImpl, a_transaction_from_another_account_is_refused)
{
    auto const contractHost = host();
    auto const fromAlice = std::make_shared<STTx const>(ttPAYMENT, [&](STObject& obj) {
        obj.setAccountID(sfAccount, alice.id());
        obj.setAccountID(sfDestination, carol.id());
        obj.setFieldAmount(sfAmount, XRP(1));
        obj.setFieldAmount(sfFee, STAmount{0});
        obj.setFieldU32(sfSequence, contractSequence());
        obj.setFieldVL(sfSigningPubKey, Blob{});
        obj.setFieldU32(sfFlags, tfInnerBatchTxn);
    });

    expectError(contractHost->emitTxn(fromAlice), HostFunctionError::SubmitTxnFailure);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.empty());
}

// One the ledger applies as a tec is queued, since its fee and sequence are spent, and its
// result is logged.
TEST_F(EmitTxnImpl, a_payment_that_claims_a_fee_is_queued_with_its_tec)
{
    auto const contractHost = host();
    auto const tooMuch = payment(XRP(1'000'000), contractSequence());

    expectValue(contractHost->emitTxn(tooMuch), TERtoInt(tecUNFUNDED_PAYMENT));
    EXPECT_EQ(contractHost.context().result.emittedTxns.size(), 1U);
    EXPECT_NE(logged().find("tecUNFUNDED_PAYMENT"), std::string::npos);
}

// A sponsor consents by signing, and an inner transaction's signatures are not checked: a
// transaction naming a sponsor is refused whole, and the account it names sponsors nothing.
TEST_F(EmitTxnImpl, a_transaction_naming_a_sponsor_is_refused)
{
    auto const contractHost = host();
    auto const tickets = std::make_shared<STTx const>(ttTICKET_CREATE, [&](STObject& obj) {
        obj.setAccountID(sfAccount, contract.id());
        obj.setFieldU32(sfTicketCount, 1);
        obj.setFieldAmount(sfFee, STAmount{0});
        obj.setFieldU32(sfSequence, contractSequence());
        obj.setFieldVL(sfSigningPubKey, Blob{});
        obj.setFieldU32(sfFlags, tfInnerBatchTxn);
        obj.setAccountID(sfSponsor, carol.id());
        obj.setFieldU32(sfSponsorFlags, spfSponsorReserve);
        STObject signature(sfSponsorSignature);
        signature.setFieldVL(sfSigningPubKey, Blob{});
        obj.set(std::move(signature));
    });

    expectError(contractHost->emitTxn(tickets), HostFunctionError::SubmitTxnFailure);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.empty());
    auto const sponsor = contractHost.context().getEmitView().read(keylet::account(carol.id()));
    ASSERT_NE(sponsor, nullptr);
    EXPECT_EQ((*sponsor)[~sfSponsoringOwnerCount].value_or(0), 0U);
}

// Sponsor flags or a sponsor signature alone are refused the same way.
TEST_F(EmitTxnImpl, sponsor_flags_or_a_sponsor_signature_alone_are_refused)
{
    auto const contractHost = host();
    auto const withFlags = std::make_shared<STTx const>(ttPAYMENT, [&](STObject& obj) {
        obj.setAccountID(sfAccount, contract.id());
        obj.setAccountID(sfDestination, carol.id());
        obj.setFieldAmount(sfAmount, XRP(1));
        obj.setFieldAmount(sfFee, STAmount{0});
        obj.setFieldU32(sfSequence, contractSequence());
        obj.setFieldVL(sfSigningPubKey, Blob{});
        obj.setFieldU32(sfFlags, tfInnerBatchTxn);
        obj.setFieldU32(sfSponsorFlags, spfSponsorFee);
    });
    auto const withSignature = std::make_shared<STTx const>(ttPAYMENT, [&](STObject& obj) {
        obj.setAccountID(sfAccount, contract.id());
        obj.setAccountID(sfDestination, carol.id());
        obj.setFieldAmount(sfAmount, XRP(1));
        obj.setFieldAmount(sfFee, STAmount{0});
        obj.setFieldU32(sfSequence, contractSequence());
        obj.setFieldVL(sfSigningPubKey, Blob{});
        obj.setFieldU32(sfFlags, tfInnerBatchTxn);
        STObject signature(sfSponsorSignature);
        signature.setFieldVL(sfSigningPubKey, Blob{});
        obj.set(std::move(signature));
    });

    expectError(contractHost->emitTxn(withFlags), HostFunctionError::SubmitTxnFailure);
    expectError(contractHost->emitTxn(withSignature), HostFunctionError::SubmitTxnFailure);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.empty());
}

// A LoanSet's counterparty consents by signing too, so a contract may not emit one.
TEST_F(EmitTxnImpl, a_loan_set_is_refused)
{
    auto const contractHost = host();
    auto const loan = std::make_shared<STTx const>(ttLOAN_SET, [&](STObject& obj) {
        obj.setAccountID(sfAccount, contract.id());
        obj.setFieldH256(sfLoanBrokerID, uint256{1});
        obj.setAccountID(sfCounterparty, carol.id());
        obj.setFieldNumber(sfPrincipalRequested, STNumber{sfPrincipalRequested, Number{100}});
        obj.setFieldAmount(sfFee, STAmount{0});
        obj.setFieldU32(sfSequence, contractSequence());
        obj.setFieldVL(sfSigningPubKey, Blob{});
        obj.setFieldU32(sfFlags, tfInnerBatchTxn);
    });

    expectError(contractHost->emitTxn(loan), HostFunctionError::SubmitTxnFailure);
    EXPECT_TRUE(contractHost.context().result.emittedTxns.empty());
}

}  // namespace xrpl::test
