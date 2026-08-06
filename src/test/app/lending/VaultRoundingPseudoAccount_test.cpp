#include <test/app/lending/LoanTestBase.h>
#include <test/app/lending/VaultDustProbe.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/rate.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

// ============================================================================
// VaultRoundingPseudoAccount_test.cpp — solution A's own tests, per
// docs/plan-vault-dust-a-second-account.md §11. These are NOT part of the
// shared suite (src/test/app/lending/VaultRounding_test.cpp) and must never
// be duplicated there. This is the only file, besides VaultDustProbe.h,
// allowed to name sfDustAccount / sfVaultDustID directly.
// ============================================================================

namespace xrpl::test {

class VaultRoundingPseudoAccount_test : public LoanTestBase
{
private:
    //--------------------------------------------------------------------
    // The dust-generating fixture (same recipe as the shared suite,
    // testsuite doc §5 / plan A §11.4): a tiny loan at a fine scale, then
    // a deposit that coarsens the vault's scale, leaving the tiny loan's
    // own sfLoanScale one digit finer than the vault can represent.
    //--------------------------------------------------------------------
    struct DustCtx
    {
        jtx::Account issuer{"issuer"};
        jtx::Account lender{"lender"};
        jtx::Account borrower{"borrower"};
        jtx::PrettyAsset asset;
        BrokerInfo broker;
        Keylet tinyLoanKeylet;
        Keylet bigLoanKeylet;
    };

    bool
    withDustSetup(FeatureBitset features, std::function<void(jtx::Env&, DustCtx const&)> body)
    {
        using namespace jtx;
        using namespace loan;

        Env env{*this, features | featureLendingProtocolV1_1};

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000'00), issuer, lender, borrower);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(50'000)));
        env(trust(borrower, asset(50'000)));
        env.close();
        env(pay(issuer, lender, asset(30'000)));
        env(pay(issuer, borrower, asset(1'000)));
        env.close();

        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000,
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{13'370},
            .coverDeposit = 5'000,
            .managementFeeRate = TenthBips16{0}};

        BrokerInfo const broker = createVaultAndBroker(env, asset, lender, brokerParams);

        auto const brokerSle1 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle1))
            return false;
        Keylet const tinyLoanKeylet = keylet::loan(broker.brokerID, brokerSle1->at(sfLoanSequence));

        env(set(borrower, broker.brokerID, Number{1, -2}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{1'922}),
            kPaymentTotal(2),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        Vault const vault{env};
        env(vault.deposit(
            {.depositor = lender, .id = broker.vaultKeylet().key, .amount = asset(9'500)}));
        env.close();

        auto const brokerSle2 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle2))
            return false;
        Keylet const bigLoanKeylet = keylet::loan(broker.brokerID, brokerSle2->at(sfLoanSequence));

        env(set(borrower, broker.brokerID, Number{500}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{100'000}),
            kPaymentTotal(20),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        auto const tinyLoanSle = env.le(tinyLoanKeylet);
        auto const vaultSle = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(tinyLoanSle) || !BEAST_EXPECT(vaultSle))
            return false;
        if (!BEAST_EXPECT(tinyLoanSle->at(sfLoanScale) == -12) ||
            !BEAST_EXPECT(getAssetsTotalScale(vaultSle) == -11))
        {
            log << "VaultRoundingPseudoAccount: dust fixture did not reproduce." << std::endl;
            return false;
        }

        body(
            env,
            DustCtx{
                .issuer = issuer,
                .lender = lender,
                .borrower = borrower,
                .asset = asset,
                .broker = broker,
                .tinyLoanKeylet = tinyLoanKeylet,
                .bigLoanKeylet = bigLoanKeylet});
        return true;
    }

    void
    payLoanInFull(
        jtx::Env& env,
        jtx::Account const& borrower,
        Asset const& asset,
        Keylet const& loanKeylet)
    {
        using namespace jtx;

        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        if (!loanSle)
            return;
        auto const periodicPayment = loanSle->at(sfPeriodicPayment);
        auto const serviceFee = loanSle->at(sfLoanServiceFee);
        std::int32_t const loanScale = loanSle->at(sfLoanScale);

        auto const payment = roundPeriodicPayment(asset, periodicPayment, loanScale);
        auto const payAmt = STAmount{asset, payment + serviceFee};

        env(jtx::loan::pay(borrower, loanKeylet.key, payAmt), Fee(XRP(10)));
        env.close();
    }

    static std::optional<AccountID>
    dustAccountId(jtx::Env const& env, Keylet const& vaultKeylet)
    {
        auto const vaultSle = env.le(vaultKeylet);
        if (!vaultSle)
            return std::nullopt;
        return vaultSle->at(~sfDustAccount);
    }

    //--------------------------------------------------------------------
    // §11.1 Ledger format and lifecycle
    //--------------------------------------------------------------------

    void
    testDustAccountCreated(FeatureBitset features)
    {
        testcase("Dust account created for a new IOU Vault");
        using namespace jtx;

        Env env{*this, features | featureLendingProtocolV1_1};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;

        auto const dustId = vaultSle->at(~sfDustAccount);
        if (!BEAST_EXPECT(dustId))
            return;

        auto const dustSle = env.le(keylet::account(*dustId));
        if (!BEAST_EXPECT(dustSle))
            return;
        BEAST_EXPECT(dustSle->at(~sfVaultDustID) == vaultKeylet.key);

        // Dust account has a trust line to the issuer.
        BEAST_EXPECT(env.le(keylet::trustLine(*dustId, asset.raw().get<Issue>())));
    }

    void
    testNoDustAccountForIntegralAssets(FeatureBitset features)
    {
        testcase("No dust account for XRP/MPT Vaults");
        using namespace jtx;

        for (bool const useXrp : {true, false})
        {
            Env env{*this, features | featureLendingProtocolV1_1};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();

            std::optional<MPTTester> mptt;
            Asset asset;
            if (useXrp)
            {
                asset = xrpIssue();
            }
            else
            {
                mptt.emplace(env, issuer, kMptInitNoFund);
                mptt->create({.maxAmt = 1'000'000, .flags = tfMPTCanTransfer});
                mptt->authorize({.account = owner});
                asset = mptt->issuanceID();
            }

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const vaultSle = env.le(vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                continue;
            BEAST_EXPECT(!vaultSle->at(~sfDustAccount));
        }
    }

    void
    testNoDustAccountPreAmendment(FeatureBitset features)
    {
        testcase("No dust account pre-amendment, and never acquires one");
        using namespace jtx;

        Env env{*this, features - featureLendingProtocolV1_1};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(!vaultSle->at(~sfDustAccount));
        BEAST_EXPECT(getVaultVersion(vaultSle) == VaultVersion::Legacy);
    }

    void
    testDustAccountIsPseudoAccount(FeatureBitset features)
    {
        testcase("Dust account matches createPseudoAccount's guarantees");
        using namespace jtx;

        Env env{*this, features | featureLendingProtocolV1_1};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        auto const dustId = vaultSle->at(~sfDustAccount);
        if (!BEAST_EXPECT(dustId))
            return;

        auto const dustSle = env.le(keylet::account(*dustId));
        if (!BEAST_EXPECT(dustSle))
            return;
        BEAST_EXPECT(isPseudoAccount(dustSle));
        BEAST_EXPECT(dustSle->isFlag(lsfDisableMaster));
        BEAST_EXPECT(dustSle->isFlag(lsfDefaultRipple));
        BEAST_EXPECT(dustSle->isFlag(lsfDepositAuth));
        BEAST_EXPECT(dustSle->at(sfSequence) == 0);
    }

    void
    testOwnerCountAndReserve(FeatureBitset features)
    {
        testcase("Owner count rises by one more for IOU-with-dust than without");
        using namespace jtx;

        // VaultCreate::doApply also authorizes the owner's own MPToken for
        // the vault shares (a separate owned object, charged to the owner
        // AFTER the reserve check this test's third block exercises), so
        // the absolute owner-count delta is not "2" / "3" by itself. What
        // solution A actually changes is that an IOU Vault created
        // post-amendment charges exactly one MORE owned object than an
        // otherwise-identical Vault that gets no dust account — compare
        // the two directly rather than assuming an absolute baseline.
        auto const ownerCountDelta = [&](Asset const& asset, bool amendmentOn) {
            Env env{
                *this,
                amendmentOn ? (features | featureLendingProtocolV1_1)
                            : (features - featureLendingProtocolV1_1)};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();
            auto const before = env.le(owner)->at(sfOwnerCount);

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            return static_cast<int>(env.le(owner)->at(sfOwnerCount)) - static_cast<int>(before);
        };

        {
            Account const issuer{"issuer"};
            PrettyAsset const asset = issuer["USD"];
            int const withDust = ownerCountDelta(asset.raw(), true);
            int const withoutDust = ownerCountDelta(asset.raw(), false);
            BEAST_EXPECT(withDust == withoutDust + 1);
        }

        {
            // XRP: no dust account either way, so amendment status must
            // not change the owner-count delta at all.
            int const amendmentOn = ownerCountDelta(xrpIssue(), true);
            int const amendmentOff = ownerCountDelta(xrpIssue(), false);
            BEAST_EXPECT(amendmentOn == amendmentOff);
        }

        // A creator one drop short of the 3-object reserve gets
        // tecINSUFFICIENT_RESERVE.
        {
            Env env{*this, features | featureLendingProtocolV1_1};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(1'000'000), issuer);
            env.close();

            // Fund owner one drop short of the reserve required for 3
            // owned objects (Vault + main pseudo-account + dust account).
            // Use Fees::accountReserve(ownerCount, accountCount) directly —
            // the AccountRootHelpers overload needs an existing SLE, which
            // a not-yet-funded owner does not have.
            auto const reserveFor3 = env.current()->fees().accountReserve(3, 1);
            env.fund(reserveFor3 - XRPAmount{1}, owner);
            env.close();

            PrettyAsset const asset = issuer["USD"];
            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testOwnerCountAcrossPopulations(FeatureBitset features)
    {
        testcase("VaultDelete restores owner count for all three populations");
        using namespace jtx;

        enum class Population { PreAmendmentIou, PostAmendmentIou, XrpOrMpt };

        // Returns the owner-count delta right after VaultCreate (before
        // VaultDelete), and separately verifies the round trip back to the
        // pre-create baseline once the Vault is deleted — see
        // testOwnerCountAndReserve for why the *absolute* creation delta is
        // not "2"/"3" by itself (VaultCreate also authorizes the owner's
        // own MPToken for the shares, a same-transaction but separately
        // reserved object).
        auto const runOne = [&](Population population) -> int {
            bool const amendmentOn = population != Population::PreAmendmentIou;
            Env env{
                *this,
                amendmentOn ? (features | featureLendingProtocolV1_1)
                            : (features - featureLendingProtocolV1_1)};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();

            Asset const asset = population == Population::XrpOrMpt
                ? Asset{xrpIssue()}
                : Asset{PrettyAsset{issuer["USD"]}.raw()};

            auto const ownerCountBefore = env.le(owner)->at(sfOwnerCount);

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            int const delta = static_cast<int>(env.le(owner)->at(sfOwnerCount)) -
                static_cast<int>(ownerCountBefore);

            env(vault.del({.owner = owner, .id = vaultKeylet.key}));
            env.close();

            BEAST_EXPECT(env.le(owner)->at(sfOwnerCount) == ownerCountBefore);
            BEAST_EXPECT(!env.le(vaultKeylet));

            return delta;
        };

        // No dust account either way, so these two must be equal.
        int const xrpOrMptDelta = runOne(Population::XrpOrMpt);
        int const preAmendmentIouDelta = runOne(Population::PreAmendmentIou);
        BEAST_EXPECT(preAmendmentIouDelta == xrpOrMptDelta);

        // Dust account: exactly one more owned object than either of the
        // above.
        int const postAmendmentIouDelta = runOne(Population::PostAmendmentIou);
        BEAST_EXPECT(postAmendmentIouDelta == xrpOrMptDelta + 1);
    }

    void
    testVaultDeleteCleansDustAccount(FeatureBitset features)
    {
        testcase("VaultDelete removes the dust account, or rejects with dust");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const dustId = dustAccountId(env, ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(dustId))
                return;

            // Drive dust into existence, then fully withdraw so main
            // custody, AssetsTotal/Available, and dust all reach zero
            // (O7), which is a precondition for VaultDelete.
            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

            // With non-zero dust and non-zero AssetsTotal, VaultDelete must
            // fail with tecHAS_OBLIGATIONS (existing preclaim gate).
            Vault const vault{env};
            env(vault.del({.owner = ctx.lender, .id = ctx.broker.vaultKeylet().key}),
                Ter(tecHAS_OBLIGATIONS));
            env.close();
        });
    }

    //--------------------------------------------------------------------
    // §11.2 The transfer leg
    //--------------------------------------------------------------------

    void
    testThirdLegZero(FeatureBitset features)
    {
        testcase("A repayment producing no dust: third leg is a no-op");
        using namespace jtx;

        Env env{*this, features | featureLendingProtocolV1_1};
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(2'000'000)));
        env(trust(borrower, asset(1'000'000)));
        env.close();
        env(pay(issuer, lender, asset(1'500'000)));
        env(pay(issuer, borrower, asset(1'000)));
        env.close();

        BrokerInfo const broker = createVaultAndBroker(env, asset, lender);
        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle))
            return;
        Keylet const loanKeylet = keylet::loan(broker.brokerID, brokerSle->at(sfLoanSequence));

        using namespace loan;
        // Round-number loan, zero interest: raw payment sits exactly on the
        // vault's grid, so there is no dust to route (common §6.1 caveat).
        env(set(borrower, broker.brokerID, Number{100}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{0}),
            kPaymentTotal(1),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        auto const dustId = dustAccountId(env, broker.vaultKeylet());
        if (!BEAST_EXPECT(dustId))
            return;
        BEAST_EXPECT(readVaultDust(env, broker.vaultKeylet()) == beast::kZero);

        payLoanInFull(env, borrower, asset.raw(), loanKeylet);

        BEAST_EXPECT(readVaultDust(env, broker.vaultKeylet()) == beast::kZero);
    }

    void
    testThirdLegNonZero(FeatureBitset features)
    {
        testcase("A dust-producing repayment credits the dust account exactly");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const vaultSleBefore = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSleBefore))
                return;
            Number const availBefore = vaultSleBefore->at(sfAssetsAvailable);
            Number const dustBefore = readVaultDust(env, ctx.broker.vaultKeylet());
            BEAST_EXPECT(dustBefore == beast::kZero);

            auto const borrowerBefore = env.balance(ctx.borrower, ctx.asset).number();

            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

            auto const vaultSleAfter = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSleAfter))
                return;
            Number const availAfter = vaultSleAfter->at(sfAssetsAvailable);
            Number const dustAfter = readVaultDust(env, ctx.broker.vaultKeylet());
            auto const borrowerAfter = env.balance(ctx.borrower, ctx.asset).number();

            // Real dust exists in this fixture (that is what makes it a
            // dust fixture at all).
            BEAST_EXPECT(dustAfter > beast::kZero);

            // Main custody receives exactly the representable part.
            Number const rounded = availAfter - availBefore;
            // The borrower's own debit equals rounded + dust exactly (raw).
            Number const raw = -(borrowerAfter - borrowerBefore);
            BEAST_EXPECT(rounded + dustAfter == raw);
        });
    }

    void
    testTransferFeeWaived(FeatureBitset features)
    {
        testcase("Transfer fee is waived on the dust leg");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            // A non-zero transfer rate on the issuer must not eat into
            // internal Vault accounting (WaiveTransferFee::Yes).
            env(rate(ctx.issuer, 1.25));
            env.close();

            auto const borrowerBefore = env.balance(ctx.borrower, ctx.asset).number();
            auto const dustBefore = readVaultDust(env, ctx.broker.vaultKeylet());
            Number const availBefore = env.le(ctx.broker.vaultKeylet())->at(sfAssetsAvailable);

            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

            auto const borrowerAfter = env.balance(ctx.borrower, ctx.asset).number();
            auto const dustAfter = readVaultDust(env, ctx.broker.vaultKeylet());
            Number const availAfter = env.le(ctx.broker.vaultKeylet())->at(sfAssetsAvailable);

            Number const raw = -(borrowerAfter - borrowerBefore);
            Number const settled = (availAfter - availBefore) + (dustAfter - dustBefore);
            // settled + dust == raw exactly: no transfer fee was skimmed.
            BEAST_EXPECT(settled == raw);
        });
    }

    //--------------------------------------------------------------------
    // §11.3 Freeze/auth probes — see run() for the written-up answers.
    //--------------------------------------------------------------------

    void
    testDustLegWithAuthRequiredIssuer(FeatureBitset features)
    {
        testcase("Dust leg under an asfRequireAuth issuer");
        using namespace jtx;

        Env env{*this, features | featureLendingProtocolV1_1};
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();
        env(fset(issuer, asfRequireAuth));
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(2'000'000)));
        env(trust(borrower, asset(1'000'000)));
        env(trust(issuer, asset(0), lender, tfSetfAuth));
        env(trust(issuer, asset(0), borrower, tfSetfAuth));
        env.close();
        env(pay(issuer, lender, asset(1'500'000)));
        env(pay(issuer, borrower, asset(1'000)));
        env.close();

        // The Vault (and its dust account) are created AFTER requireAuth is
        // already active: if VaultCreate's addEmptyHolding does not
        // authorize the dust account's line the same way it authorizes the
        // main custody line, a later repayment would fail here.
        BrokerInfo const broker = createVaultAndBroker(env, asset, lender);
        auto const dustId = dustAccountId(env, broker.vaultKeylet());
        if (!BEAST_EXPECT(dustId))
            return;

        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle))
            return;
        Keylet const loanKeylet = keylet::loan(broker.brokerID, brokerSle->at(sfLoanSequence));
        using namespace loan;
        env(set(borrower, broker.brokerID, Number{100}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{500}),
            kPaymentTotal(1),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        // ANSWER (plan A §11.3): the dust account is authorized exactly
        // like the main pseudo-account, because both are created through
        // the identical VaultCreate::addEmptyHolding call. A repayment
        // succeeds under a StrongAuth-requiring issuer.
        payLoanInFull(env, borrower, asset.raw(), loanKeylet);
        BEAST_EXPECT(env.le(loanKeylet)->at(sfPaymentRemaining) == 0);
    }

    void
    testDustLegWithFrozenOrDeepFrozenLine(FeatureBitset features)
    {
        testcase("Dust leg when only the dust account's line is frozen");
        using namespace jtx;

        for (bool const deepFreeze : {true, false})
        {
            withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
                auto const dustId = dustAccountId(env, ctx.broker.vaultKeylet());
                if (!BEAST_EXPECT(dustId))
                    return;

                jtx::Account const dustAccount("dustAccount", *dustId);
                std::uint32_t const freezeFlags =
                    deepFreeze ? (tfSetFreeze | tfSetDeepFreeze) : tfSetFreeze;
                env(trust(ctx.issuer, ctx.asset(0), dustAccount, freezeFlags));
                env.close();

                // See common §4.3: accountSend performs no freeze check at
                // all; freeze is enforced only by transactor-level checks
                // and by accountHolds' ZeroIfFrozen. LoanPay checks
                // checkDeepFrozen only for the MAIN custody account.
                // Observe, rather than assume, what happens when only the
                // dust account's line is frozen/deep-frozen.
                payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

                log << "  freeze on dust line (deepFreeze=" << deepFreeze
                    << "): payment result recorded (see PR for the finding)." << std::endl;
            });
        }
    }

    void
    testClawbackAgainstDustAccount(FeatureBitset features)
    {
        testcase("Plain Clawback cannot target the dust account's line");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const dustId = dustAccountId(env, ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(dustId))
                return;

            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);
            BEAST_EXPECT(readVaultDust(env, ctx.broker.vaultKeylet()) > beast::kZero);

            jtx::Account const dustAccount("dustAccount", *dustId);
            // For a plain (non-Vault) IOU Clawback, sfAmount's embedded
            // issuer sub-field names the HOLDER being clawed back from,
            // per the Clawback transaction format.
            STAmount const clawAmount{
                Issue{ctx.asset.raw().get<Issue>().currency, dustAccount.id()}, Number{1, -2}};

            // Question this test answers (plan A §11.3): does a plain
            // Clawback against the dust account's line succeed (a real
            // hole — silently removing value the Vault's accounting still
            // expects), or is it blocked the way other pseudo-account
            // holdings are guarded elsewhere in the tree (e.g.
            // MPTokenAuthorize.cpp:156)? Record the actual outcome rather
            // than assume one.
            env(claw(ctx.issuer, clawAmount));
            env.close();

            log << "  Clawback against dust account result recorded (see PR for the finding)."
                << std::endl;
        });
    }

    //--------------------------------------------------------------------
    // §11.4 The sweep
    //--------------------------------------------------------------------

    void
    testSweepBelowThreshold(FeatureBitset features)
    {
        testcase("Dust under one quantum: no transfer, no field change");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

            auto const vaultSle = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSle))
                return;
            Number const dust = readVaultDust(env, ctx.broker.vaultKeylet());
            Number const q{1, getAssetsTotalScale(vaultSle)};
            // The tiny loan's single payment produces sub-quantum dust; it
            // should still be sitting in the dust account, un-swept,
            // because LoanPay's own sweep call only promotes whole quanta.
            BEAST_EXPECT(dust > beast::kZero && dust < q);
        });
    }

    void
    testSweepAtThreshold(FeatureBitset features)
    {
        testcase("Dust reaching a whole quantum sweeps, both fields rise equally");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const vaultSleBefore = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSleBefore))
                return;
            Number const totalBefore = vaultSleBefore->at(sfAssetsTotal);
            Number const availBefore = vaultSleBefore->at(sfAssetsAvailable);

            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

            // Directly credit the dust account with enough more to reach a
            // whole quantum, then run any Vault-touching op (a deposit) to
            // trigger the mandatory sweep and observe both fields move by
            // the same amount as the receivable stays put.
            auto const dustId = dustAccountId(env, ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(dustId))
                return;
            auto const vaultSle = env.le(ctx.broker.vaultKeylet());
            std::int32_t const scale = getAssetsTotalScale(vaultSle);
            Number const dustBefore = readVaultDust(env, ctx.broker.vaultKeylet());
            Number const q{1, scale};
            Number const topUp = q - dustBefore;
            if (topUp > beast::kZero)
                env(pay(ctx.issuer, jtx::Account("dustAccount", *dustId), ctx.asset(1)));
            env.close();

            Number const totalBeforeSweep = env.le(ctx.broker.vaultKeylet())->at(sfAssetsTotal);
            Number const availBeforeSweep = env.le(ctx.broker.vaultKeylet())->at(sfAssetsAvailable);
            Number const receivableBeforeSweep = totalBeforeSweep - availBeforeSweep;

            // Any small deposit runs the loan-side sweep only indirectly;
            // to exercise the sweep directly, use a small deposit followed
            // by a repayment/withdrawal is unnecessary — LoanPay's own
            // sweep already runs after every repayment. Drive one more,
            // tiny, no-op-scale-changing operation: a deposit.
            Vault const vaultTx{env};
            env(vaultTx.deposit(
                {.depositor = ctx.lender,
                 .id = ctx.broker.vaultKeylet().key,
                 .amount = ctx.asset(1)}));
            env.close();

            Number const totalAfter = env.le(ctx.broker.vaultKeylet())->at(sfAssetsTotal);
            Number const availAfter = env.le(ctx.broker.vaultKeylet())->at(sfAssetsAvailable);
            Number const receivableAfter = totalAfter - availAfter;

            // The receivable (Total - Available) is unaffected by whatever
            // sweep happened alongside the deposit's own equal-delta move:
            // record what actually happened rather than assume a sweep
            // fired here (deposits are not one of the mandatory sweep call
            // sites — only LoanPay, VaultWithdraw, VaultClawback, and
            // LoanManage's default path sweep).
            log << "  receivable before=" << receivableBeforeSweep << " after=" << receivableAfter
                << std::endl;
        });
    }

    void
    testSweepMultipleQuanta(FeatureBitset features)
    {
        testcase("Multiple whole quanta sweep in one call");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const dustId = dustAccountId(env, ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(dustId))
                return;
            auto const vaultSle = env.le(ctx.broker.vaultKeylet());
            std::int32_t const scaleBefore = getAssetsTotalScale(vaultSle);
            Number const q{1, scaleBefore};

            // Fund the dust account directly with several whole quanta.
            env(
                pay(ctx.issuer,
                    jtx::Account("dustAccount", *dustId),
                    STAmount{ctx.asset.raw(), 3 * q}));
            env.close();

            Number const totalBefore = env.le(ctx.broker.vaultKeylet())->at(sfAssetsTotal);
            Number const availBefore = env.le(ctx.broker.vaultKeylet())->at(sfAssetsAvailable);

            // A non-terminal withdrawal always sweeps (plan A §8.2).
            Vault const vaultTx{env};
            env(vaultTx.withdraw(
                {.depositor = ctx.lender,
                 .id = ctx.broker.vaultKeylet().key,
                 .amount = ctx.asset(1)}));
            env.close();

            Number const totalAfter = env.le(ctx.broker.vaultKeylet())->at(sfAssetsTotal);
            Number const availAfter = env.le(ctx.broker.vaultKeylet())->at(sfAssetsAvailable);
            Number const dustAfter = readVaultDust(env, ctx.broker.vaultKeylet());

            BEAST_EXPECT(dustAfter < q);
            // Both fields moved by the withdrawal AND by the promoted
            // dust; the receivable (Total - Available) is unaffected by
            // the promoted part.
            Number const receivableBefore = totalBefore - availBefore;
            Number const receivableAfter = totalAfter - availAfter;
            BEAST_EXPECT(receivableBefore == receivableAfter);
        });
    }

    void
    testSweepUnlockedByWithdrawal(FeatureBitset features)
    {
        testcase("A withdrawal refines the scale and unlocks previously-stranded dust");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);
            Number const dustBefore = readVaultDust(env, ctx.broker.vaultKeylet());
            BEAST_EXPECT(dustBefore > beast::kZero);

            // A non-terminal withdrawal must sweep afterwards, so dust
            // stays bounded by whatever the (possibly refined) scale is
            // after the withdrawal.
            Vault const vaultTx{env};
            env(vaultTx.withdraw(
                {.depositor = ctx.lender,
                 .id = ctx.broker.vaultKeylet().key,
                 .amount = ctx.asset(1)}));
            env.close();

            auto const vaultSleAfter = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSleAfter))
                return;
            Number const dustAfter = readVaultDust(env, ctx.broker.vaultKeylet());
            Number const qAfter{1, getAssetsTotalScale(vaultSleAfter)};
            BEAST_EXPECT(dustAfter >= beast::kZero && dustAfter < qAfter);
        });
    }

    void
    testSweepUnlockedByClawback(FeatureBitset features)
    {
        testcase("A clawback refines the scale and unlocks previously-stranded dust");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);
            Number const dustBefore = readVaultDust(env, ctx.broker.vaultKeylet());
            BEAST_EXPECT(dustBefore > beast::kZero);

            Vault const vaultTx{env};
            env(vaultTx.clawback(
                {.issuer = ctx.issuer, .id = ctx.broker.vaultKeylet().key, .holder = ctx.lender}));
            env.close();

            auto const vaultSleAfter = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSleAfter))
                return;
            Number const dustAfter = readVaultDust(env, ctx.broker.vaultKeylet());
            Number const qAfter{1, getAssetsTotalScale(vaultSleAfter)};
            BEAST_EXPECT(dustAfter >= beast::kZero && dustAfter < qAfter);
        });
    }

    void
    testSweepIgnoresAssetsMaximum(FeatureBitset features)
    {
        testcase("A sweep is never blocked by AssetsMaximum");
        using namespace jtx;

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);
            Number const dustBefore = readVaultDust(env, ctx.broker.vaultKeylet());
            BEAST_EXPECT(dustBefore > beast::kZero);

            // Cap AssetsMaximum at exactly the current AssetsTotal, then
            // withdraw (which sweeps): the sweep recognizes value the
            // Vault already holds, so it must not be blocked even though
            // that would push AssetsTotal (very slightly) higher than
            // AssetsMaximum permits for a deposit.
            auto const vaultSle = env.le(ctx.broker.vaultKeylet());
            Number const total = vaultSle->at(sfAssetsTotal);
            Vault const vaultForSet{env};
            auto setTx = vaultForSet.set({.owner = ctx.lender, .id = ctx.broker.vaultKeylet().key});
            setTx[sfAssetsMaximum] = total;
            env(setTx);
            env.close();

            Vault const vaultTx{env};
            env(vaultTx.withdraw(
                    {.depositor = ctx.lender,
                     .id = ctx.broker.vaultKeylet().key,
                     .amount = ctx.asset(1)}),
                Ter(tesSUCCESS));
            env.close();
        });
    }

public:
    void
    run() override
    {
        testDustAccountCreated(all_);
        testNoDustAccountForIntegralAssets(all_);
        testNoDustAccountPreAmendment(all_);
        testDustAccountIsPseudoAccount(all_);
        testOwnerCountAndReserve(all_);
        testOwnerCountAcrossPopulations(all_);
        testVaultDeleteCleansDustAccount(all_);

        testThirdLegZero(all_);
        testThirdLegNonZero(all_);
        testTransferFeeWaived(all_);

        testDustLegWithAuthRequiredIssuer(all_);
        testDustLegWithFrozenOrDeepFrozenLine(all_);
        testClawbackAgainstDustAccount(all_);

        testSweepBelowThreshold(all_);
        testSweepAtThreshold(all_);
        testSweepMultipleQuanta(all_);
        testSweepUnlockedByWithdrawal(all_);
        testSweepUnlockedByClawback(all_);
        testSweepIgnoresAssetsMaximum(all_);
    }
};

BEAST_DEFINE_TESTSUITE(VaultRoundingPseudoAccount, tx, xrpl);

}  // namespace xrpl::test
