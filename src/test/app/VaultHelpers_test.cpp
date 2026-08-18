#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl::test {

// Exercises the addVaultAssets/clawbackVaultAssets/removeVaultAssets/
// moveVaultAssets helpers directly, against a real Vault SLE and real
// account/trust-line state built via jtx, but bypassing the VaultDeposit/
// VaultWithdraw/VaultClawback/LoanSet transactors entirely. This lets each
// helper's contract (field deltas, transfer amounts, FinalRemoval behavior)
// be checked in isolation, in addition to the transactor-level coverage in
// Vault_test.cpp, LoanSet_test.cpp, etc.
class VaultHelpers_test : public beast::unit_test::Suite
{
    // Builds a public IOU-backed vault owned by `owner`. Leaves the Env
    // with no pending (un-applied) transactions, so callers can safely
    // follow up with raw ApplyViewImpl mutations.
    static Keylet
    setupVault(jtx::Env& env, jtx::PrettyAsset const& asset, jtx::Account const& owner)
    {
        jtx::Vault const v{env};
        auto const [createTx, vaultKeylet] = v.create({.owner = owner, .asset = asset});
        env(createTx);
        env.close();
        return vaultKeylet;
    }

    void
    testAddVaultAssets()
    {
        testcase("addVaultAssets");
        using namespace jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(10'000), issuer, owner, depositor);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(owner, asset(1'000'000)));
        env(trust(depositor, asset(1'000'000)));
        env(pay(issuer, depositor, asset(10'000)));
        env.close();

        auto const vaultKeylet = setupVault(env, asset, owner);

        Vault const v{env};
        env(v.deposit({.depositor = depositor, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        auto const open = env.current();
        ApplyViewImpl view(&*open, TapNone);
        auto const vault = view.peek(vaultKeylet);
        if (!BEAST_EXPECT(vault))
            return;
        Asset const vaultAsset = vault->at(sfAsset);

        Number const totalBefore = vault->at(sfAssetsTotal);
        Number const availableBefore = vault->at(sfAssetsAvailable);
        auto const depositorBalanceBefore = accountHolds(
            view,
            depositor,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);

        // valueDelta and amount are independent: a nonzero valueDelta with a
        // zero amount recognizes a value change (e.g. a covered default)
        // without transferring any cash.
        {
            STAmount const zero{vaultAsset, 0};
            STAmount const fifty{vaultAsset, 50};
            auto const ter = addVaultAssets(view, vault, depositor, zero, fifty, env.journal);
            Number const totalAfter = vault->at(sfAssetsTotal);
            Number const availableAfter = vault->at(sfAssetsAvailable);
            auto const depositorBalanceAfter = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(totalAfter == totalBefore + 50);
            BEAST_EXPECT(availableAfter == availableBefore);
            BEAST_EXPECT(depositorBalanceAfter == depositorBalanceBefore);
        }

        // The common case: amount == valueDelta, and cash actually moves
        // from `sender` to the Vault's pseudo-account.
        {
            STAmount const hundred{vaultAsset, 100};
            auto const ter = addVaultAssets(view, vault, depositor, hundred, hundred, env.journal);
            Number const totalAfter = vault->at(sfAssetsTotal);
            Number const availableAfter = vault->at(sfAssetsAvailable);
            auto const depositorBalanceAfter = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(totalAfter == totalBefore + 150);
            BEAST_EXPECT(availableAfter == availableBefore + 100);
            BEAST_EXPECT(depositorBalanceAfter == depositorBalanceBefore - hundred);
        }

        // valueDelta may be negative (e.g. a small rounding correction on a
        // loan payment), independently of amount again.
        {
            Number const totalBeforeNegative = vault->at(sfAssetsTotal);
            Number const availableBeforeNegative = vault->at(sfAssetsAvailable);
            STAmount const zero{vaultAsset, 0};
            STAmount const minusTen{vaultAsset, -10};
            auto const ter = addVaultAssets(view, vault, depositor, zero, minusTen, env.journal);
            Number const totalAfter = vault->at(sfAssetsTotal);
            Number const availableAfter = vault->at(sfAssetsAvailable);
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(totalAfter == totalBeforeNegative - 10);
            BEAST_EXPECT(availableAfter == availableBeforeNegative);
        }

        // If the underlying accountSend fails (here: sender has no trust
        // line for the vault asset and is not its issuer, so cannot source
        // the IOU), the helper propagates the non-tes error rather than
        // silently swallowing it. The Vault SLE fields have already been
        // mutated by the time accountSend fails (the helper follows the
        // "mutate-then-transfer" ordering documented in VaultHelpers.h);
        // this test pins that observable so the ordering contract is
        // visible and future changes have to knowingly break it. In
        // production the transactor's ApplyView sandbox is discarded on a
        // non-tesSUCCESS return, so the caller never observes the
        // mutation.
        {
            Account const stranger{"stranger"};
            env.fund(XRP(10'000), stranger);
            env.close();
            Number const totalBeforeFail = vault->at(sfAssetsTotal);
            Number const availableBeforeFail = vault->at(sfAssetsAvailable);
            STAmount const ten{vaultAsset, 10};
            auto const ter = addVaultAssets(view, vault, stranger, ten, ten, env.journal);
            BEAST_EXPECT(!isTesSuccess(ter));
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == totalBeforeFail + 10);
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == availableBeforeFail + 10);
        }
    }

    void
    testClawbackVaultAssets()
    {
        testcase("clawbackVaultAssets");
        using namespace jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(10'000), issuer, owner, depositor);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(owner, asset(1'000'000)));
        env(trust(depositor, asset(1'000'000)));
        env(pay(issuer, depositor, asset(10'000)));
        env.close();

        auto const vaultKeylet = setupVault(env, asset, owner);

        Vault const v{env};
        env(v.deposit({.depositor = depositor, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        auto const open = env.current();
        ApplyViewImpl view(&*open, TapNone);
        auto const vault = view.peek(vaultKeylet);
        if (!BEAST_EXPECT(vault))
            return;
        Asset const vaultAsset = vault->at(sfAsset);

        Number const totalBefore = vault->at(sfAssetsTotal);
        Number const availableBefore = vault->at(sfAssetsAvailable);
        AccountID const vaultAccount = vault->at(sfAccount);
        // The issuer of an IOU always implicitly holds it (no trust line
        // required), matching the only real caller (VaultClawback, where
        // the recipient is always the asset's issuer); accountHolds for an
        // issuer's own currency doesn't track a normal incrementable
        // balance, so check the Vault's own (real, decrementable) balance
        // instead.
        auto const vaultBalanceBefore = accountHolds(
            view,
            vaultAccount,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);

        STAmount const hundred{vaultAsset, 100};
        auto const ter = clawbackVaultAssets(view, vault, issuer, hundred, env.journal);
        Number const totalAfter = vault->at(sfAssetsTotal);
        Number const availableAfter = vault->at(sfAssetsAvailable);
        auto const vaultBalanceAfter = accountHolds(
            view,
            vaultAccount,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);

        BEAST_EXPECT(isTesSuccess(ter));
        BEAST_EXPECT(totalAfter == totalBefore - 100);
        BEAST_EXPECT(availableAfter == availableBefore - 100);
        BEAST_EXPECT(vaultBalanceAfter == vaultBalanceBefore - hundred);

        // Clawing back more than sfAssetsAvailable fails outright, leaving
        // the Vault's fields untouched.
        {
            Number const totalBeforeFail = vault->at(sfAssetsTotal);
            Number const availableBeforeFail = vault->at(sfAssetsAvailable);
            STAmount const tooMuch{vaultAsset, availableBeforeFail + 1};
            auto const failTer = clawbackVaultAssets(view, vault, issuer, tooMuch, env.journal);
            BEAST_EXPECT(failTer == tefINTERNAL);
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == totalBeforeFail);
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == availableBeforeFail);
        }

        // If the underlying accountSend fails (here: recipient has no trust
        // line for the vault asset and cannot receive it), the helper
        // propagates the non-tes error rather than silently swallowing it.
        // In production the recipient is always the asset issuer, which
        // implicitly holds its own asset; this synthetic third-party
        // recipient stands in only to exercise the failure branch. As with
        // addVaultAssets, the Vault SLE fields are mutated (decreased)
        // before the transfer attempt, matching the documented mutate-
        // then-transfer contract; the transactor sandbox is what makes the
        // mutation invisible to callers on failure.
        {
            Account const stranger{"stranger"};
            env.fund(XRP(10'000), stranger);
            env.close();
            Number const totalBeforeFail = vault->at(sfAssetsTotal);
            Number const availableBeforeFail = vault->at(sfAssetsAvailable);
            STAmount const ten{vaultAsset, 10};
            auto const failTer = clawbackVaultAssets(view, vault, stranger, ten, env.journal);
            BEAST_EXPECT(!isTesSuccess(failTer));
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == totalBeforeFail - 10);
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == availableBeforeFail - 10);
        }
    }

    void
    testRemoveVaultAssets()
    {
        testcase("removeVaultAssets");
        using namespace jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        // Third-party withdrawal destination for the dstAcct != senderAcct
        // sub-test below. Set up before the ApplyViewImpl snapshot so its
        // account root and trust line are visible to the helper.
        Account const charlie{"charlie"};
        env.fund(XRP(10'000), issuer, owner, depositor, charlie);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(owner, asset(1'000'000)));
        env(trust(depositor, asset(1'000'000)));
        env(trust(charlie, asset(1'000'000)));
        env(pay(issuer, depositor, asset(10'000)));
        env.close();

        auto const vaultKeylet = setupVault(env, asset, owner);

        Vault const v{env};
        env(v.deposit({.depositor = depositor, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        // doWithdraw uses ctx.tx on the third-party-destination path (both
        // verifyDepositPreauth and getEffectiveTxReserveSponsor read it);
        // for a self-withdrawal (senderAcct == dstAcct) it is unused. A
        // signed noop from `depositor` stands in for a real VaultWithdraw
        // transaction here: verifyDepositPreauth passes for a destination
        // without lsfDepositAuth, and getEffectiveTxReserveSponsor returns
        // a null sponsor because the destination differs from tx.Account
        // (see SponsorHelpers.cpp:getEffectiveTxReserveSponsor).
        auto const dummyTx = env.jt(noop(depositor)).stx;
        if (!BEAST_EXPECT(dummyTx))
            return;

        auto const open = env.current();
        ApplyViewImpl view(&*open, TapNone);
        auto const vault = view.peek(vaultKeylet);
        if (!BEAST_EXPECT(vault))
            return;
        Asset const vaultAsset = vault->at(sfAsset);
        ApplyViewContext const ctx{.view = view, .tx = *dummyTx};

        Number const totalBefore = vault->at(sfAssetsTotal);
        Number const availableBefore = vault->at(sfAssetsAvailable);
        auto const depositorBalanceBefore = accountHolds(
            view,
            depositor,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);

        // Normal (non-final) removal: both fields decrease by `amount`.
        {
            STAmount const hundred{vaultAsset, 100};
            auto const ter = removeVaultAssets(
                ctx,
                vault,
                depositor,
                depositor,
                XRPAmount{0},
                hundred,
                env.journal,
                FinalRemoval::No);
            Number const totalAfter = vault->at(sfAssetsTotal);
            Number const availableAfter = vault->at(sfAssetsAvailable);
            auto const depositorBalanceAfter = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);

            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(totalAfter == totalBefore - 100);
            BEAST_EXPECT(availableAfter == availableBefore - 100);
            BEAST_EXPECT(depositorBalanceAfter == depositorBalanceBefore + hundred);
        }

        // Third-party destination: senderAcct != dstAcct exercises
        // doWithdraw's verifyDepositPreauth branch (a self-withdrawal
        // instead hits addEmptyHolding). `charlie` already holds a trust
        // line for the asset, so no holding creation is needed on the
        // destination side. The Vault SLE moves by `amount`, and the
        // funds land on `charlie` rather than on the transaction sender.
        {
            Number const totalBefore3P = vault->at(sfAssetsTotal);
            Number const availableBefore3P = vault->at(sfAssetsAvailable);
            auto const charlieBalanceBefore3P = accountHolds(
                view,
                charlie,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            auto const depositorBalanceBefore3P = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);

            STAmount const fifty{vaultAsset, 50};
            auto const ter = removeVaultAssets(
                ctx, vault, depositor, charlie, XRPAmount{0}, fifty, env.journal, FinalRemoval::No);
            auto const charlieBalanceAfter3P = accountHolds(
                view,
                charlie,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            auto const depositorBalanceAfter3P = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);

            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == totalBefore3P - 50);
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == availableBefore3P - 50);
            BEAST_EXPECT(charlieBalanceAfter3P == charlieBalanceBefore3P + fifty);
            BEAST_EXPECT(depositorBalanceAfter3P == depositorBalanceBefore3P);
        }

        // FinalRemoval::Yes hard-resets both fields to exactly zero and
        // drains the Vault's pseudo-account: the caller must pass
        // `amount == pre-call sfAssetsAvailable` (VaultWithdraw pins that
        // via `assetsWithdrawn = allAvailable` before setting the flag),
        // and the recipient's balance is expected to increase by exactly
        // that amount. The passed-in STAmount is used only for the
        // pseudo-account -> recipient transfer; the Vault fields are
        // reset regardless of it (as long as it matches sfAssetsAvailable
        // -- other values would trip the helper's precondition
        // XRPL_ASSERT and are covered by the transactor contract, not
        // this unit).
        {
            Number const totalBeforeFinal = vault->at(sfAssetsTotal);
            Number const availableBeforeFinal = vault->at(sfAssetsAvailable);
            auto const depositorBalanceBeforeFinal = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);

            STAmount const allAvailable{vaultAsset, availableBeforeFinal};
            auto const ter = removeVaultAssets(
                ctx,
                vault,
                depositor,
                depositor,
                XRPAmount{0},
                allAvailable,
                env.journal,
                FinalRemoval::Yes);
            auto const depositorBalanceAfterFinal = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);

            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == Number{0});
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == Number{0});
            BEAST_EXPECT(depositorBalanceAfterFinal == depositorBalanceBeforeFinal + allAvailable);
            BEAST_EXPECTS(
                totalBeforeFinal != Number(0),
                "fixture sanity: total was nonzero before the final removal");
        }
    }

    void
    testMoveVaultAssets()
    {
        testcase("moveVaultAssets");
        using namespace jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const borrower{"borrower"};
        Account const feeRecipient{"feeRecipient"};
        env.fund(XRP(10'000), issuer, owner, depositor, borrower, feeRecipient);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(owner, asset(1'000'000)));
        env(trust(depositor, asset(1'000'000)));
        env(trust(borrower, asset(1'000'000)));
        env(trust(feeRecipient, asset(1'000'000)));
        env(pay(issuer, depositor, asset(10'000)));
        env.close();

        auto const vaultKeylet = setupVault(env, asset, owner);

        Vault const v{env};
        env(v.deposit({.depositor = depositor, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        auto const open = env.current();
        ApplyViewImpl view(&*open, TapNone);
        auto const vault = view.peek(vaultKeylet);
        if (!BEAST_EXPECT(vault))
            return;
        Asset const vaultAsset = vault->at(sfAsset);

        Number const totalBefore = vault->at(sfAssetsTotal);
        Number const availableBefore = vault->at(sfAssetsAvailable);
        auto const borrowerBalanceBefore = accountHolds(
            view,
            borrower,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
        auto const feeRecipientBalanceBefore = accountHolds(
            view,
            feeRecipient,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);

        // Mimics a loan disbursement: principal to the borrower, a fee to a
        // separate recipient. valueDelta is zero here since a nonzero value
        // is only meaningful (and only permitted, see moveVaultAssets's
        // assertion) for a Legacy-version Vault recognizing accrued
        // interest into sfAssetsTotal at origination; this fixture's Vault
        // is not Legacy. sfAssetsAvailable still decreases independently of
        // sfAssetsTotal, which is the contract under test.
        MultiplePaymentDestinations const recipients{
            {borrower, Number{80}},
            {feeRecipient, Number{20}},
        };
        STAmount const zero{vaultAsset, 0};
        auto const ter = moveVaultAssets(view, vault, recipients, zero, env.journal);
        Number const totalAfter = vault->at(sfAssetsTotal);
        Number const availableAfter = vault->at(sfAssetsAvailable);
        auto const borrowerBalanceAfter = accountHolds(
            view,
            borrower,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
        auto const feeRecipientBalanceAfter = accountHolds(
            view,
            feeRecipient,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
        STAmount const eighty{vaultAsset, 80};
        STAmount const twenty{vaultAsset, 20};

        BEAST_EXPECT(isTesSuccess(ter));
        BEAST_EXPECT(totalAfter == totalBefore);
        BEAST_EXPECT(availableAfter == availableBefore - 100);
        BEAST_EXPECT(borrowerBalanceAfter == borrowerBalanceBefore + eighty);
        BEAST_EXPECT(feeRecipientBalanceAfter == feeRecipientBalanceBefore + twenty);

        // Zero-amount recipients still count toward the recipients.size() > 1
        // precondition and drive the sum-of-amounts to zero, which short-
        // circuits the accountSendMulti call. The Vault's fields are still
        // updated (both to their pre-call values, since the deltas are all
        // zero), and no funds move.
        {
            Number const totalBeforeZero = vault->at(sfAssetsTotal);
            Number const availableBeforeZero = vault->at(sfAssetsAvailable);
            auto const borrowerBalanceBeforeZero = accountHolds(
                view,
                borrower,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            MultiplePaymentDestinations const zeroRecipients{
                {borrower, Number{0}},
                {feeRecipient, Number{0}},
            };
            auto const zeroTer = moveVaultAssets(view, vault, zeroRecipients, zero, env.journal);
            auto const borrowerBalanceAfterZero = accountHolds(
                view,
                borrower,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            BEAST_EXPECT(isTesSuccess(zeroTer));
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == totalBeforeZero);
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == availableBeforeZero);
            BEAST_EXPECT(borrowerBalanceAfterZero == borrowerBalanceBeforeZero);
        }
    }

    // Legacy-version Vault fixture: featureLendingProtocolV1_1 is off, so
    // VaultCreate does not set sfLEVersion and getVaultVersion() resolves
    // to Legacy. That is the only vault variant on which
    // moveVaultAssets permits a nonzero valueDelta (see the
    // "moveVaultAssets : nonzero valueDelta requires Legacy vault version"
    // assertion in the helper).
    void
    testMoveVaultAssetsLegacy()
    {
        testcase("moveVaultAssets: Legacy vault, nonzero valueDelta");
        using namespace jtx;

        Env env(*this, testableAmendments() - featureLendingProtocolV1_1);
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const borrower{"borrower"};
        Account const feeRecipient{"feeRecipient"};
        env.fund(XRP(10'000), issuer, owner, depositor, borrower, feeRecipient);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(owner, asset(1'000'000)));
        env(trust(depositor, asset(1'000'000)));
        env(trust(borrower, asset(1'000'000)));
        env(trust(feeRecipient, asset(1'000'000)));
        env(pay(issuer, depositor, asset(10'000)));
        env.close();

        auto const vaultKeylet = setupVault(env, asset, owner);

        Vault const v{env};
        env(v.deposit({.depositor = depositor, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        auto const open = env.current();
        ApplyViewImpl view(&*open, TapNone);
        auto const vault = view.peek(vaultKeylet);
        if (!BEAST_EXPECT(vault))
            return;
        // Confirm the fixture actually produced a Legacy vault (sfLEVersion
        // absent). If this changes upstream the helper's precondition would
        // reject the nonzero valueDelta below, which would only fire via an
        // XRPL_ASSERT — checking it explicitly keeps the failure mode
        // legible.
        if (!BEAST_EXPECTS(
                getVaultVersion(vault) == VaultVersion::Legacy,
                "fixture: featureLendingProtocolV1_1 disabled produces Legacy vault"))
            return;

        Asset const vaultAsset = vault->at(sfAsset);

        Number const totalBefore = vault->at(sfAssetsTotal);
        Number const availableBefore = vault->at(sfAssetsAvailable);
        auto const borrowerBalanceBefore = accountHolds(
            view,
            borrower,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
        auto const feeRecipientBalanceBefore = accountHolds(
            view,
            feeRecipient,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);

        // Nonzero valueDelta increases sfAssetsTotal independently of the
        // funds actually moved from the Vault's pseudo-account to the
        // recipients: this mirrors a Legacy-vault loan origination, where
        // recognizing accrued interest into sfAssetsTotal is decoupled
        // from the cash disbursement decrementing sfAssetsAvailable.
        MultiplePaymentDestinations const recipients{
            {borrower, Number{80}},
            {feeRecipient, Number{20}},
        };
        STAmount const valueDelta{vaultAsset, 5};
        auto const ter = moveVaultAssets(view, vault, recipients, valueDelta, env.journal);
        auto const borrowerBalanceAfter = accountHolds(
            view,
            borrower,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
        auto const feeRecipientBalanceAfter = accountHolds(
            view,
            feeRecipient,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
        STAmount const eighty{vaultAsset, 80};
        STAmount const twenty{vaultAsset, 20};

        BEAST_EXPECT(isTesSuccess(ter));
        BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == totalBefore + 5);
        BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == availableBefore - 100);
        BEAST_EXPECT(borrowerBalanceAfter == borrowerBalanceBefore + eighty);
        BEAST_EXPECT(feeRecipientBalanceAfter == feeRecipientBalanceBefore + twenty);
    }

    // MPT-backed Vault exercises add/remove helpers on an integral asset,
    // where accountSend/doWithdraw take the MPT code paths instead of the
    // IOU trust-line paths used by the primary testAddVaultAssets/
    // testRemoveVaultAssets fixtures. Field-mutation contracts are
    // asset-agnostic and stay identical, but this confirms the helpers
    // compose correctly with the MPT transfer implementations.
    void
    testHelpersMPT()
    {
        testcase("addVaultAssets / removeVaultAssets: MPT-backed vault");
        using namespace jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(10'000), issuer, owner, depositor);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        mptt.authorize({.account = owner});
        mptt.authorize({.account = depositor});
        PrettyAsset const asset = mptt.issuanceID();
        env(pay(issuer, depositor, asset(10'000)));
        env.close();

        auto const vaultKeylet = setupVault(env, asset, owner);

        Vault const v{env};
        env(v.deposit({.depositor = depositor, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        auto const dummyTx = env.jt(noop(depositor)).stx;
        if (!BEAST_EXPECT(dummyTx))
            return;

        auto const open = env.current();
        ApplyViewImpl view(&*open, TapNone);
        auto const vault = view.peek(vaultKeylet);
        if (!BEAST_EXPECT(vault))
            return;
        Asset const vaultAsset = vault->at(sfAsset);
        ApplyViewContext const ctx{.view = view, .tx = *dummyTx};

        Number const totalBefore = vault->at(sfAssetsTotal);
        Number const availableBefore = vault->at(sfAssetsAvailable);
        auto const depositorBalanceBefore = accountHolds(
            view,
            depositor,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);

        // addVaultAssets: same field-delta contract as the IOU fixture,
        // but accountSend routes through the MPT transfer code path.
        {
            STAmount const fifty{vaultAsset, 50};
            auto const ter = addVaultAssets(view, vault, depositor, fifty, fifty, env.journal);
            auto const depositorBalanceAfter = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == totalBefore + 50);
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == availableBefore + 50);
            BEAST_EXPECT(depositorBalanceAfter == depositorBalanceBefore - fifty);
        }

        // removeVaultAssets (non-final, self-withdrawal): fields decrease
        // by amount; doWithdraw's MPT path routes the transfer back to
        // the depositor.
        {
            Number const totalBeforeRm = vault->at(sfAssetsTotal);
            Number const availableBeforeRm = vault->at(sfAssetsAvailable);
            auto const depositorBalanceBeforeRm = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            STAmount const thirty{vaultAsset, 30};
            auto const ter = removeVaultAssets(
                ctx,
                vault,
                depositor,
                depositor,
                XRPAmount{0},
                thirty,
                env.journal,
                FinalRemoval::No);
            auto const depositorBalanceAfterRm = accountHolds(
                view,
                depositor,
                vaultAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == totalBeforeRm - 30);
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == availableBeforeRm - 30);
            BEAST_EXPECT(depositorBalanceAfterRm == depositorBalanceBeforeRm + thirty);
        }
    }

public:
    void
    run() override
    {
        testAddVaultAssets();
        testClawbackVaultAssets();
        testRemoveVaultAssets();
        testMoveVaultAssets();
        testMoveVaultAssetsLegacy();
        testHelpersMPT();
    }
};

BEAST_DEFINE_TESTSUITE(VaultHelpers, app, xrpl);

}  // namespace xrpl::test
