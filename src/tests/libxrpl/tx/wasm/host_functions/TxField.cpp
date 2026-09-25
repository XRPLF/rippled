#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <cstdint>
#include <iterator>
#include <utility>

namespace xrpl::test {

struct TxFieldImpl : RealHostFixture
{
    template <typename Functor>
    void
    checkTxField(Account const& acct, SField const& field, TxAssembler assembler, Functor&& f)
    {
        auto h = makeHost(keylet::account(acct.id()), assembler.type, std::move(assembler.build));
        expectValue(h->getTxField(field), f());
    }

    void
    checkTxFieldError(
        Account const& acct,
        SField const& field,
        TxAssembler assembler,
        HostFunctionError error)
    {
        auto h = makeHost(keylet::account(acct.id()), assembler.type, std::move(assembler.build));
        expectError(h->getTxField(field), error);
    }
};

TEST_F(TxFieldImpl, mptoken_issuance_create_tx_matches_scale)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto const expectedScale = std::uint8_t{8};
    checkTxField(owner, sfAssetScale, mptIssuanceCreateTx(owner, expectedScale), [&] {
        return RealHostFixture::toBytes(expectedScale);
    });
}

TEST_F(TxFieldImpl, amm_deposit_tx_usd_matches_asset)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto usdIssue = Issue{toCurrency("USD"), owner.id()};
    checkTxField(
        owner, sfAsset, ammDepositTx(owner, xrpIssue(), usdIssue), [&] { return Bytes(20, 0); });
}

TEST_F(TxFieldImpl, amm_deposit_tx_usd_matches_asset2)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto usdIssue = Issue{toCurrency("USD"), owner.id()};
    checkTxField(owner, sfAsset2, ammDepositTx(owner, xrpIssue(), usdIssue), [&] {
        return RealHostFixture::toBytes(Asset{usdIssue});
    });
}

TEST_F(TxFieldImpl, amm_deposit_tx_gbp_matches_asset)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto gbpIssue = Issue{toCurrency("GBP"), owner.id()};
    auto mptId = makeMptID(1, owner);
    auto mptIssue = MPTIssue{mptId};
    checkTxField(owner, sfAsset, ammDepositTx(owner, gbpIssue, mptIssue), [&] {
        return RealHostFixture::toBytes(Asset{gbpIssue});
    });
}

TEST_F(TxFieldImpl, amm_deposit_tx_gbp_matches_asset2)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto gbpIssue = Issue{toCurrency("GBP"), owner.id()};
    auto mptId = makeMptID(1, owner);
    auto mptIssue = MPTIssue{mptId};
    checkTxField(owner, sfAsset2, ammDepositTx(owner, gbpIssue, mptIssue), [&] {
        return RealHostFixture::toBytes(Asset{mptId});
    });
}

TEST_F(TxFieldImpl, escrow_tx_matches_account)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxField(owner, sfAccount, escrowFinishTx(ledger, owner), [&] {
        return Bytes{std::begin(owner.id()), std::end(owner.id())};
    });
}

TEST_F(TxFieldImpl, escrow_tx_matches_owner)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxField(owner, sfOwner, escrowFinishTx(ledger, owner), [&] {
        return Bytes{std::begin(owner.id()), std::end(owner.id())};
    });
}

TEST_F(TxFieldImpl, escrow_tx_matches_transaction_type)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxField(owner, sfTransactionType, escrowFinishTx(ledger, owner), [] {
        return RealHostFixture::toBytes(ttESCROW_FINISH);
    });
}

TEST_F(TxFieldImpl, escrow_tx_matches_offer_sequence)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxField(owner, sfOfferSequence, escrowFinishTx(ledger, owner), [&] {
        return RealHostFixture::toBytes(ledger.getAccountRoot(owner.id()).getSequence());
    });
}

TEST_F(TxFieldImpl, escrow_tx_matches_destination)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxFieldError(
        owner, sfDestination, escrowFinishTx(ledger, owner), HostFunctionError::FieldNotFound);
}

TEST_F(TxFieldImpl, escrow_tx_matches_memos)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxFieldError(
        owner, sfMemos, escrowFinishTx(ledger, owner), HostFunctionError::NotLeafField);
}

TEST_F(TxFieldImpl, escrow_tx_matches_credential_ids)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxFieldError(
        owner, sfCredentialIDs, escrowFinishTx(ledger, owner), HostFunctionError::NotLeafField);
}

TEST_F(TxFieldImpl, escrow_tx_matches_invalid)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxFieldError(
        owner, sfInvalid, escrowFinishTx(ledger, owner), HostFunctionError::FieldNotFound);
}

TEST_F(TxFieldImpl, escrow_tx_matches_generic)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    checkTxFieldError(
        owner, sfGeneric, escrowFinishTx(ledger, owner), HostFunctionError::FieldNotFound);
}

}  // namespace xrpl::test
