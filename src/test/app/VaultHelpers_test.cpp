#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
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
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>

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
        // silently swallowing it.
        {
            Account const stranger{"stranger"};
            env.fund(XRP(10'000), stranger);
            env.close();
            STAmount const ten{vaultAsset, 10};
            auto const ter = addVaultAssets(view, vault, stranger, ten, ten, env.journal);
            BEAST_EXPECT(!isTesSuccess(ter));
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
        // recipient stands in only to exercise the failure branch.
        {
            Account const stranger{"stranger"};
            env.fund(XRP(10'000), stranger);
            env.close();
            STAmount const ten{vaultAsset, 10};
            auto const failTer = clawbackVaultAssets(view, vault, stranger, ten, env.journal);
            BEAST_EXPECT(!isTesSuccess(failTer));
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

        // doWithdraw only reads ctx.tx on the third-party-destination path;
        // for a self-withdrawal (senderAcct == dstAcct, exercised below) it
        // is unused, so a trivial signed noop stands in for a real
        // VaultWithdraw transaction.
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

        // FinalRemoval::Yes hard-resets both fields to exactly zero,
        // regardless of the passed-in amount (deliberately an understated
        // amount here, to prove the fields aren't merely decremented by
        // it).
        {
            Number const totalBeforeFinal = vault->at(sfAssetsTotal);
            STAmount const one{vaultAsset, 1};
            auto const ter = removeVaultAssets(
                ctx,
                vault,
                depositor,
                depositor,
                XRPAmount{0},
                one,
                env.journal,
                FinalRemoval::Yes);
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(Number(vault->at(sfAssetsTotal)) == Number{0});
            BEAST_EXPECT(Number(vault->at(sfAssetsAvailable)) == Number{0});
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

public:
    void
    run() override
    {
        testAddVaultAssets();
        testClawbackVaultAssets();
        testRemoveVaultAssets();
        testMoveVaultAssets();
    }
};

BEAST_DEFINE_TESTSUITE(VaultHelpers, app, xrpl);

}  // namespace xrpl::test
