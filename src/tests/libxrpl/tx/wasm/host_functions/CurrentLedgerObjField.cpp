#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct CurrentLedgerObjFieldImpl : WasmImplTest
{
    // Create an escrow owned by `owner` and return its keylet (the object the host will
    // read as its "current" object).
    Keylet
    makeEscrow(Account const& owner, Account const& dest, uint256* transactionId = nullptr)
    {
        ledger.createAccount(owner, XRP(1000));
        ledger.createAccount(dest, XRP(1000));

        auto const ownerSeq = ledger.getAccountRoot(owner.id()).getSequence();
        // A finish time comfortably after the genesis close time.
        auto const r = ledger.submit(
            transactions::EscrowCreateBuilder{owner.id(), dest.id(), XRP(100)}.setFinishAfter(
                900'000'000),
            owner);
        EXPECT_EQ(r.ter, tesSUCCESS) << transToken(r.ter);
        if (transactionId != nullptr)
        {
            *transactionId = r.tx->getTransactionID();
        }
        ledger.close();
        return keylet::escrow(owner.id(), SeqProxy::rawSequence(ownerSeq));
    }
};

TEST_F(CurrentLedgerObjFieldImpl, ReadsfAccount)
{
    auto const owner = Account{"owner"};
    auto const escrow = makeEscrow(owner, Account{"dest"});
    ASSERT_NE(ledger.getOpenLedger().read(escrow), nullptr) << "escrow object should exist";

    expectValue(makeHost(escrow)->getCurrentLedgerObjField(sfAccount), toBytes(owner.id()));
}

TEST_F(CurrentLedgerObjFieldImpl, ReadsfAccountDummyEscrow)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto const ownerSeq = ledger.getAccountRoot(owner.id()).getSequence();
    auto const escrow = keylet::escrow(owner.id(), SeqProxy::rawSequence(ownerSeq));

    expectError(
        makeHost(escrow)->getCurrentLedgerObjField(sfAccount),
        HostFunctionError::LedgerObjNotFound);
}

TEST_F(CurrentLedgerObjFieldImpl, ReadAmount)
{
    auto const owner = Account{"owner"};
    auto const escrow = makeEscrow(owner, Account{"dest"});
    ASSERT_NE(ledger.getOpenLedger().read(escrow), nullptr) << "escrow object should exist";

    expectValue(makeHost(escrow)->getCurrentLedgerObjField(sfAmount), toBytes(XRP(100)));
}

TEST_F(CurrentLedgerObjFieldImpl, ReadPreviousTxnID)
{
    auto const owner = Account{"owner"};
    auto transactionId = uint256{};
    auto const escrow = makeEscrow(owner, Account{"dest"}, &transactionId);
    ASSERT_NE(ledger.getOpenLedger().read(escrow), nullptr) << "escrow object should exist";

    expectValue(
        makeHost(escrow)->getCurrentLedgerObjField(sfPreviousTxnID), toBytes(transactionId));
}

TEST_F(CurrentLedgerObjFieldImpl, ReadOwner)
{
    auto const owner = Account{"owner"};
    auto const escrow = makeEscrow(owner, Account{"dest"});
    ASSERT_NE(ledger.getOpenLedger().read(escrow), nullptr) << "escrow object should exist";

    expectError(
        makeHost(escrow)->getCurrentLedgerObjField(sfOwner), HostFunctionError::FieldNotFound);
}

}  // namespace xrpl::test
