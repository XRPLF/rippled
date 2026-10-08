#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace xrpl::test {

class LoanValidation_test : public LoanTestBase
{
private:
    void
    testDisabled()
    {
        testcase("Disabled");
        // Run with combinations of the Lending Protocol, Single Asset Vault
        // and MPTokensV1 amendments disabled.
        using namespace jtx;
        auto failAll = [this](FeatureBitset features) {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, bob);

            auto const keylet = keylet::loanBroker(alice, SeqProxy::rawSequence(env.seq(alice)));

            using namespace std::chrono_literals;
            using namespace loan;

            // LoanSet without a counterparty signature is rejected with
            // temDISABLED.
            auto setTx = env.jt(set(alice, keylet.key, Number(10000)), Ter(temDISABLED));
            env(setTx);

            // All loan transactions are disabled.
            // 1. LoanSet
            setTx = env.jt(setTx, Sig(sfCounterpartySignature, bob), Ter(temDISABLED));
            env(setTx);
            // A placeholder loan keylet.
            auto const loanKeylet = keylet::loan(keylet.key, SeqProxy::rawSequence(env.seq(alice)));
            // Other Loan transactions are disabled, too.
            // 2. LoanDelete
            env(del(alice, loanKeylet.key), Ter(temDISABLED));
            // 3. LoanManage
            env(manage(alice, loanKeylet.key, tfLoanImpair), Ter(temDISABLED));
            // 4. LoanPay
            env(pay(alice, loanKeylet.key, XRP(500)), Ter(temDISABLED));
        };
        failAll(all_ - featureMPTokensV1);
        failAll(all_ - featureSingleAssetVault - featureLendingProtocol);
        failAll(all_ - featureSingleAssetVault);
        failAll(all_ - featureLendingProtocol);
    }

    void
    testInvalidLoanSet(VaultKind vaultKind)
    {
        testcase(
            std::string("Invalid LoanSet (") +
            (vaultKind == VaultKind::OpenEnded ? "open-ended" : "closed-ended") + " vault)");
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        Account const sponsor{"sponsor"};
        auto const iou = issuer["IOU"];

        auto testWrapper = [&](auto&& test) {
            Env env(*this, all_ | featureLendingProtocolV1_1 | featureLendingProtocolV1_2);
            env.fund(XRP(1'000), lender, issuer, borrower, sponsor);
            env(trust(lender, iou(10'000'000)));
            env(pay(issuer, lender, iou(5'000'000)));
            BrokerInfo const brokerInfo{
                createVaultAndBroker(env, issuer["IOU"], lender, {.vaultKind = vaultKind})};

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            Number const debtMaximumRequest = brokerInfo.asset(1'000).value();
            test(env, brokerInfo, loanSetFee, debtMaximumRequest);
        };

        // preflight:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            for (auto const sponsorFlags : {spfSponsorReserve, spfSponsorReserve | spfSponsorFee})
            {
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    sponsor::As(sponsor, sponsorFlags),
                    Sig(sfCounterpartySignature, lender),
                    loanSetFee,
                    Ter(temINVALID_FLAG));
            }

            // LoanSet::preflight called directly rejects sponsored-reserve
            // flags with temINVALID_FLAG.
            for (auto const sponsorFlags : {spfSponsorReserve, spfSponsorReserve | spfSponsorFee})
            {
                auto const jtx = env.jt(
                    set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    sponsor::As(sponsor, sponsorFlags),
                    Sig(sfCounterpartySignature, lender),
                    loanSetFee);
                if (BEAST_EXPECT(jtx.stx))
                {
                    PreflightContext const pfCtx(
                        env.app(), *jtx.stx, env.current()->rules(), TapNone, env.journal);
                    BEAST_EXPECT(LoanSet::preflight(pfCtx) == temINVALID_FLAG);
                }
            }

            // Neither CounterpartySignature nor Borrower, and not a Batch
            // inner: rejected with temBAD_SIGNER.
            env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                loanSetFee,
                Ter(temBAD_SIGNER));

            // StartDate without Borrower is rejected with temBAD_SIGNER.
            env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                kStartDate((env.now() + 1h).time_since_epoch().count()),
                loanSetFee,
                Ter(temBAD_SIGNER));

            // Borrower without StartDate is rejected with temINVALID.
            env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                kBorrower(borrower),
                loanSetFee,
                Ter(temINVALID));

            // A proposal naming the proposer as Borrower is rejected with
            // temINVALID. The proposer is not the LoanBroker owner, which
            // shows this check fires before the preclaim ownership check.
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                kBorrower(borrower),
                kStartDate((env.now() + 1h).time_since_epoch().count()),
                loanSetFee,
                Ter(temINVALID));

            // invalid grace period
            {
                // zero grace period
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kGracePeriod(0),
                    loanSetFee,
                    Ter(temINVALID));

                // grace period less than default minimum
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kGracePeriod(LoanSet::kDefaultGracePeriod - 1),
                    loanSetFee,
                    Ter(temINVALID));

                // grace period greater than payment interval
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kPaymentInterval(120),
                    kGracePeriod(121),
                    loanSetFee,
                    Ter(temINVALID));
            }
            // empty/zero broker ID
            {
                auto jv = set(borrower, UInt256{}, debtMaximumRequest);

                auto testZeroBrokerID = [&](std::string const& id, std::uint32_t flags = 0) {
                    // empty broker ID
                    jv[sfLoanBrokerID] = id;
                    env(jv,
                        Sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        Txflags(flags),
                        Ter(temINVALID));
                };
                // empty broker ID
                testZeroBrokerID(std::string(""));
                // zero broker ID; the flag makes the STTx distinct from the
                // one above
                testZeroBrokerID(to_string(UInt256{}), tfFullyCanonicalSig);
            }

            // Borrower together with Counterparty is rejected with temINVALID.
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                kBorrower(borrower),
                kCounterparty(lender),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(temINVALID));

            // Borrower together with CounterpartySignature is rejected with
            // temINVALID.
            env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                kBorrower(borrower),
                Sig(sfCounterpartySignature, borrower),
                loanSetFee,
                Ter(temINVALID));

            // A corrupted counterparty SigningPubKey fails local signature
            // checks at submit.
            JTx const tx = env.jt(
                set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee);
            STTx local = *(tx.stx);
            auto counterpartySig = local.getFieldObject(sfCounterpartySignature);
            auto badPubKey = counterpartySig.getFieldVL(sfSigningPubKey);
            badPubKey[20] ^= 0xAA;
            counterpartySig.setFieldVL(sfSigningPubKey, badPubKey);
            local.setFieldObject(sfCounterpartySignature, counterpartySig);
            json::Value jvResult;
            jvResult[jss::tx_blob] = strHex(local.getSerializer().slice());
            auto res = env.rpc("json", "submit", to_string(jvResult))["result"];
            BEAST_EXPECT(
                res[jss::error] == "invalidTransaction" &&
                res[jss::error_exception] ==
                    "fails local checks: Counterparty: Invalid signature.");
        });

        // preclaim:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            std::uint32_t const futureDate = (env.now() + 1h).time_since_epoch().count();

            // A two-step proposal whose payment schedule overflows the
            // protocol time limit is rejected with tecKILLED.
            {
                using TimeType = decltype(sfNextPaymentDueDate)::type::value_type;
                static_assert(std::is_same_v<TimeType, std::uint32_t>);
                constexpr TimeType kMaxTime = std::numeric_limits<TimeType>::max();
                static_assert(kMaxTime == 4'294'967'295);
                constexpr std::uint32_t kPayTotal = 10;
                constexpr std::uint32_t kPayInterval = 200;

                // PaymentInterval alone exceeds kMaxTime - StartDate.
                env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                    kBorrower(borrower),
                    kStartDate(kMaxTime - (kPayInterval - 1)),
                    kPaymentTotal(kPayTotal),
                    kPaymentInterval(kPayInterval),
                    loanSetFee,
                    Ter(tecKILLED));

                // Interval fits but interval * total exceeds the remaining
                // time available for the schedule.
                env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                    kBorrower(borrower),
                    kStartDate(kMaxTime - (kPayInterval * kPayTotal / 2)),
                    kPaymentTotal(kPayTotal),
                    kPaymentInterval(kPayInterval),
                    loanSetFee,
                    Ter(tecKILLED));
            }

            // A two-step proposal from an account other than the LoanBroker
            // owner is rejected with tecNO_PERMISSION.
            env(set(sponsor, brokerInfo.brokerID, debtMaximumRequest),
                kBorrower(borrower),
                kStartDate(futureDate),
                loanSetFee,
                Ter(tecNO_PERMISSION));

            // Naming a pseudo-account as the Borrower is rejected with
            // tecNO_PERMISSION.
            {
                auto const vaultPseudo = [&]() {
                    auto const v = env.le(brokerInfo.vaultKeylet());
                    return Account("vault pseudo-account", v->at(sfAccount));
                }();
                auto const brokerPseudo = [&]() {
                    auto const b = env.le(brokerInfo.brokerKeylet());
                    return Account("broker pseudo-account", b->at(sfAccount));
                }();
                for (auto const& pseudo : {vaultPseudo, brokerPseudo})
                {
                    env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                        kBorrower(pseudo),
                        kStartDate(futureDate),
                        loanSetFee,
                        Ter(tecNO_PERMISSION));
                }
            }

            // A proposal naming a Borrower that does not exist is rejected
            // with tecNO_DST.
            env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                kBorrower(Account("nobody")),
                kStartDate(futureDate),
                loanSetFee,
                Ter(tecNO_DST));

            // With the issuer's DefaultRipple cleared, LoanSet is rejected
            // with terNO_RIPPLE (IOU only).
            env(fclear(issuer, asfDefaultRipple));
            env.close();
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(terNO_RIPPLE));
        });

        // doApply:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            auto const amt =
                env.balance(borrower) - accountReserve(*env.current(), borrower.id(), env.journal);
            env(pay(borrower, issuer, amt));

            // tecINSUFFICIENT_RESERVE
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(tecINSUFFICIENT_RESERVE));

            // addEmptyHolding failure
            env(pay(issuer, borrower, amt));
            env(fset(issuer, asfGlobalFreeze));
            env.close();

            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(tecFROZEN));
        });

        // doApply: tecMAX_SEQUENCE_REACHED
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            // With the broker's LoanSequence at its maximum, the next LoanSet
            // is rejected with tecMAX_SEQUENCE_REACHED.
            auto const changed =
                env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
                    Sandbox sb(&view, TapNone);
                    auto broker = sb.peek(brokerInfo.brokerKeylet());
                    if (!broker)
                        return false;
                    broker->setFieldU32(sfLoanSequence, std::numeric_limits<std::uint32_t>::max());
                    sb.update(broker);
                    sb.apply(view);
                    return true;
                });
            BEAST_EXPECT(changed);

            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(tecMAX_SEQUENCE_REACHED));
        });
    }

    // A LoanSet with a fractional IOU value field, on a vault whose loanScale
    // is whole units, is rejected with tecPRECISION_LOSS in doApply. A
    // FixedPrecision vault's loanScale is its base scale, so the vault is
    // created with sfScale 0 to pin loanScale to whole units. One field per
    // iteration.
    void
    testLoanSetDoApplyPrecisionLoss()
    {
        using namespace jtx;
        using namespace loan;

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        // 1.5 units: representable as an IOU amount, not at loanScale 0.
        Number const kFractionalUnits{15, -1};

        auto const runCase = [&, this](char const* label, auto const& fieldSetter) {
            testcase << "LoanSet doApply precision-loss: " << label;

            Env env(*this, all_ | featureLendingProtocolV1_1 | featureLendingProtocolV1_2);
            PrettyAsset const iouAsset = createFundedRippleIouAsset(env, issuer, lender, borrower);
            BrokerInfo const brokerInfo{createVaultAndBroker(
                env,
                iouAsset,
                lender,
                {.vaultDeposit = 100'000,
                 .debtMax = 25'000,
                 .managementFeeRate = TenthBips16{1000},
                 .vaultScale = 0})};

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            Number const kLegalPrincipal{1'000};

            env(set(borrower, brokerInfo.brokerID, kLegalPrincipal),
                Sig(sfCounterpartySignature, lender),
                kInterestRate(TenthBips32{10'000}),
                kPaymentTotal(12),
                kPaymentInterval(60),
                kGracePeriod(60),
                fieldSetter(kFractionalUnits),
                loanSetFee,
                Ter(tecPRECISION_LOSS));
            env.close();
        };

        runCase("sfLoanOriginationFee", kLoanOriginationFee);
        runCase("sfLoanServiceFee", kLoanServiceFee);
        runCase("sfLatePaymentFee", kLatePaymentFee);
        runCase("sfClosePaymentFee", kClosePaymentFee);
    }

    void
    testInvalidLoanDelete()
    {
        testcase("Invalid LoanDelete");
        using namespace jtx;
        using namespace loan;

        // preflight: temINVALID, LoanID == zero
        {
            Account const alice{"alice"};
            Env env(*this);
            env.fund(XRP(1'000), alice);
            env.close();
            env(del(alice, beast::kZero), Ter(temINVALID));
        }
    }

    void
    testInvalidLoanManage()
    {
        testcase("Invalid LoanManage");
        using namespace jtx;
        using namespace loan;

        // preflight: temINVALID, LoanID == zero
        {
            Account const alice{"alice"};
            Env env(*this);
            env.fund(XRP(1'000), alice);
            env.close();
            env(manage(alice, beast::kZero, tfLoanDefault), Ter(temINVALID));
        }
    }

    void
    testInvalidLoanAccept()
    {
        testcase("Invalid LoanAccept");
        using namespace jtx;
        using namespace loan;

        // Preflight and preclaim guards of LoanAccept. Failures that need a
        // pending loan are covered in testTwoStepValidation.
        Account const alice{"alice"};
        Env env(*this, all_ | featureLendingProtocolV1_1 | featureLendingProtocolV1_2);
        env.fund(XRP(1'000), alice);
        env.close();

        // A zero LoanID is rejected with temINVALID.
        env(accept(alice, beast::kZero), Ter(temINVALID));

        auto const bogusLoanID = keylet::loan(uint256{1}, SeqProxy::rawSequence(1)).key;

        // Any non-universal flag is rejected with temINVALID_FLAG.
        env(accept(alice, bogusLoanID, tfLoanImpair), Ter(temINVALID_FLAG));

        // A LoanID that does not exist is rejected with tecNO_ENTRY.
        env(accept(alice, bogusLoanID), Ter(tecNO_ENTRY));
    }

    void
    testInvalidLoanPay()
    {
        testcase("Invalid LoanPay");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        auto const iou = issuer["IOU"];

        // preclaim
        Env env(*this);
        env.fund(XRP(1'000), lender, issuer, borrower);
        env(trust(lender, iou(10'000'000)));
        env(pay(issuer, lender, iou(5'000'000)));
        BrokerInfo brokerInfo{createVaultAndBroker(env, issuer["IOU"], lender)};

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

        env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
            Sig(sfCounterpartySignature, lender),
            loanSetFee);

        env.close();

        std::uint32_t const loanSequence = 1;
        auto const loanKeylet =
            keylet::loan(brokerInfo.brokerID, SeqProxy::rawSequence(loanSequence));

        env(fset(issuer, asfGlobalFreeze));
        env.close();

        // preclaim: tecFROZEN
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecFROZEN));
        env.close();

        env(fclear(issuer, asfGlobalFreeze));
        env.close();

        auto const pseudoBroker = [&]() -> std::optional<Account> {
            if (auto brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                return Account{"pseudo", brokerSle->at(sfAccount)};
            }

            return std::nullopt;
        }();
        if (!pseudoBroker)
            return;

        // Lender and pseudoaccount must both be frozen
        env(trust(issuer, lender["IOU"](1'000), lender, tfSetFreeze | tfSetDeepFreeze));
        env(trust(
            issuer, (*pseudoBroker)["IOU"](1'000), *pseudoBroker, tfSetFreeze | tfSetDeepFreeze));
        env.close();

        // preclaim: tecFROZEN due to deep frozen
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecFROZEN));
        env.close();

        // Only one needs to be unfrozen
        env(trust(issuer, lender["IOU"](1'000), tfClearFreeze | tfClearDeepFreeze));
        env.close();

        // Advance one more close, past the due date. A late payment without
        // tfLoanLatePayment is rejected with tecEXPIRED.
        env.close();

        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecEXPIRED));
        env.close();
        env(pay(borrower, loanKeylet.key, debtMaximumRequest, tfLoanLatePayment));
        env.close();

        // preclaim: tecKILLED
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecKILLED));
    }

    // Accounts and loan terms shared by the two-step helpers below. The lender
    // owns the Vault and LoanBroker and proposes loans to the borrower.
    jtx::Account const issuer_{"issuer"};
    jtx::Account const lender_{"lender"};
    jtx::Account const borrower_{"borrower"};
    TenthBips32 const interest_{50'000};
    std::uint32_t const payTotal_{10};
    std::uint32_t const payInterval_{200};

    // Build a funded environment with a Vault + LoanBroker owned by lender_,
    // using the requested asset type, and return the broker.
    BrokerInfo
    makeTwoStepBroker(jtx::Env& env, AssetType assetType)
    {
        using namespace jtx;
        env.fund(XRP(100'000'000), noripple(lender_));
        env.fund(XRP(1'000'000), borrower_);
        if (assetType != AssetType::XRP)
            env.fund(XRP(1'000'000), issuer_);
        env.close();
        BrokerParameters const params{};
        auto const asset = createAsset(env, assetType, params, issuer_, lender_, borrower_);
        env.close();
        if (!asset.native())
            env(pay(issuer_, lender_, asset(params.vaultDeposit + params.coverDeposit)));
        env.close();
        return createVaultAndBroker(env, asset, lender_, params);
    }

    // Submit a two-step proposal from lender_ to borrower_ with the supplied
    // StartDate and any extra functors, such as the expected result.
    template <typename... Extra>
    void
    propose(jtx::Env& env, BrokerInfo const& broker, std::uint32_t startDate, Extra const&... extra)
    {
        using namespace jtx;
        using namespace loan;
        env(set(lender_, broker.brokerID, broker.asset(200).number()),
            kBorrower(borrower_),
            kStartDate(startDate),
            kInterestRate(interest_),
            kPaymentTotal(payTotal_),
            kPaymentInterval(payInterval_),
            extra...);
    }

    // The keylet of the next loan the broker will create.
    static Keylet
    nextLoanKeylet(jtx::Env& env, BrokerInfo const& broker)
    {
        auto const brokerSle = env.le(broker.brokerKeylet());
        return keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
    }

    // Two-step checks that need a proposal to reach the ledger: the StartDate
    // expiry boundary, the pending-loan interlocks with LoanManage / LoanPay,
    // and the LoanDelete recovery path for a proposal whose StartDate has
    // passed. Field and participant checks for a two-step LoanSet live in
    // testInvalidLoanSet.
    void
    testTwoStepValidation()
    {
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        FeatureBitset const features =
            all_ | featureLendingProtocolV1_1 | featureLendingProtocolV1_2;

        {
            testcase("Two-step: LoanSet StartDate expiry boundary");

            // StartDate == parentCloseTime counts as expired;
            // StartDate == parentCloseTime + 1 does not.
            Env env(*this, features);
            auto const broker = makeTwoStepBroker(env, AssetType::XRP);

            auto const parentClose = env.current()->parentCloseTime().time_since_epoch().count();

            // StartDate == parentCloseTime is rejected with tecEXPIRED.
            propose(env, broker, parentClose, Ter(tecEXPIRED));

            // StartDate == parentCloseTime + 1 succeeds.
            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, parentClose + 1);
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
        }

        {
            testcase("Two-step: pending loan rejects other transactions");

            // A pending loan rejects every loan transaction other than
            // LoanAccept and LoanDelete.
            Env env(*this, features);
            auto const broker = makeTwoStepBroker(env, AssetType::XRP);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // The loan is pending.
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));

            // LoanManage can not impair, unimpair, or default a pending loan.
            env(manage(lender_, loanKeylet.key, tfLoanImpair), Ter(tecNO_PERMISSION));
            env(manage(lender_, loanKeylet.key, tfLoanUnimpair), Ter(tecNO_PERMISSION));
            env(manage(lender_, loanKeylet.key, tfLoanDefault), Ter(tecNO_PERMISSION));

            // LoanPay can not pay a pending loan, even from the borrower.
            env(pay(borrower_, loanKeylet.key, broker.asset(50)), Ter(tecNO_PERMISSION));
            env(pay(borrower_, loanKeylet.key, broker.asset(50), tfLoanFullPayment),
                Ter(tecNO_PERMISSION));

            // The borrower can still accept the pending loan.
            env(accept(borrower_, loanKeylet.key));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
        }

        // LoanManage on a pending loan is rejected with tecNO_PERMISSION even
        // after NextPaymentDueDate + GracePeriod has passed.
        for (auto const assetType : {AssetType::XRP, AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: pending loan rejects LoanManage after due date ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeTwoStepBroker(env, assetType);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, startDate);
            env.close();

            // Advance well past StartDate + PaymentInterval + GracePeriod.
            env.close(NetClock::time_point{NetClock::duration{startDate}} + 2h);

            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(
                    env.now() > NetClock::time_point{NetClock::duration{
                                    loan->at(sfNextPaymentDueDate) + loan->at(sfGracePeriod)}});
            }

            env(manage(lender_, loanKeylet.key, tfLoanDefault), Ter(tecNO_PERMISSION));
            env(manage(lender_, loanKeylet.key, tfLoanImpair), Ter(tecNO_PERMISSION));
            env(manage(lender_, loanKeylet.key, tfLoanUnimpair), Ter(tecNO_PERMISSION));
        }

        {
            testcase("Two-step: LoanDelete of pending loan after StartDate expired");

            // A pending loan whose StartDate has passed can no longer be
            // accepted (LoanAccept returns tecEXPIRED), but it can still be
            // cleaned up with LoanDelete, releasing the reserve and reversing
            // the vault bookkeeping.
            Env env(*this, features);
            auto const broker = makeTwoStepBroker(env, AssetType::XRP);

            auto const vault0 = env.le(broker.vaultKeylet());
            auto const lenderOwners0 = env.ownerCount(lender_);
            auto const borrowerOwners0 = env.ownerCount(borrower_);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, startDate);
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0 + 1);

            // Advance the ledger beyond the StartDate.
            env.close(NetClock::time_point{NetClock::duration{startDate}} + 1h);

            // The expired proposal can no longer be accepted, and the failed
            // LoanAccept leaves it in place and still pending.
            env(accept(borrower_, loanKeylet.key), Ter(tecEXPIRED));
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));

            // But it can still be deleted.
            env(del(lender_, loanKeylet.key));
            env.close();

            // The loan is gone, the reserve is released, and the vault
            // bookkeeping is fully reversed.
            BEAST_EXPECT(!env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0);
            BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners0);

            if (auto const vault1 = env.le(broker.vaultKeylet()); BEAST_EXPECT(vault0 && vault1))
            {
                BEAST_EXPECT(vault1->at(sfAssetsAvailable) == vault0->at(sfAssetsAvailable));
                BEAST_EXPECT(vault1->at(sfAssetsReserved) == vault0->at(sfAssetsReserved));
                BEAST_EXPECT(vault1->at(sfAssetsTotal) == vault0->at(sfAssetsTotal));
            }
        }
    }

    void
    testRequireAuth()
    {
        testcase("Require Auth - Implicit Pseudo-account authorization");
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};

        // Run both creation flows. In each, an unauthorized borrower is
        // rejected with tecNO_AUTH.
        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            bool const twoStep = flow == LoanFlow::TwoStep;

            Env env(*this, all_ | featureLendingProtocolV1_1 | featureLendingProtocolV1_2);

            env.fund(XRP(100'000), issuer, lender, borrower);
            env.close();

            auto asset = MPTTester({
                .env = env,
                .issuer = issuer,
                .holders = {lender, borrower},
                .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanClawback | tfMPTCanLock,
                .authHolder = true,
            });

            env(pay(issuer, lender, asset(5'000'000)));
            BrokerInfo brokerInfo{createVaultAndBroker(env, asset, lender)};

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

            auto forUnauthAuth = [&](auto&& doTx) {
                for (auto const flag : {tfMPTUnauthorize, 0u})
                {
                    asset.authorize({.account = issuer, .holder = borrower, .flags = flag});
                    env.close();
                    doTx(flag == 0);
                    env.close();
                }
            };

            static constexpr std::uint32_t kLoanSequence = 1;
            auto const loanKeylet =
                keylet::loan(brokerInfo.brokerID, SeqProxy::rawSequence(kLoanSequence));

            // Can't create a loan if the borrower is not authorized
            forUnauthAuth([&](bool authorized) {
                auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
                if (twoStep)
                {
                    env(set(lender, brokerInfo.brokerID, debtMaximumRequest),
                        kBorrower(borrower),
                        kStartDate((env.now() + 1h).time_since_epoch().count()),
                        loanSetFee,
                        err);
                }
                else
                {
                    env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                        Sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        err);
                }
            });

            // In the two-step flow, the borrower accepts the pending loan.
            if (twoStep)
            {
                env(accept(borrower, loanKeylet.key));
                env.close();
            }

            // Can't loan pay if the borrower is not authorized
            forUnauthAuth([&](bool authorized) {
                auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
                env(pay(borrower, loanKeylet.key, debtMaximumRequest), err);
            });
        }
    }

    void
    testLimitExceeded()
    {
        testcase("RIPD-4125 - overpayment");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{200000, -6},
            .interest = TenthBips32{50000},
            .payTotal = 3,
            .payInterval = 200,
            .gracePd = 60,
            .flags = tfLoanOverpayment,
        };

        auto const assetType = AssetType::XRP;

        Env env(*this, makeConfig(), all_, nullptr, beast::Severity::Warning);

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        auto const state = getCurrentState(env, broker, loanKeylet);

        env(loan::pay(
            borrower,
            loanKeylet.key,
            STAmount{broker.asset, state.periodicPayment * 3 / 2 + 1},
            tfLoanOverpayment));
        env.close();

        PaymentParameters const paymentParams{
            .showStepBalances = false,
            .validateBalances = true,
        };

        makeLoanPayments(
            env,
            broker,
            loanParams,
            loanKeylet,
            verifyLoanStatus,
            issuer,
            lender,
            borrower,
            paymentParams);
    }

    void
    testWrongMaxDebtBehavior(FeatureBitset features)
    {
        // From FIND-003
        testcase << "Wrong Max Debt Behavior";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};

        BrokerParameters const brokerParams{.debtMax = 0};
        env.fund(XRP(brokerParams.vaultDeposit * 100), issuer, noripple(lender));
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        if (auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfDebtMaximum) == 0);
        }

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        // The lender is both the borrower and the counterparty.
        auto const createJson = env.json(
            set(lender, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            Fee(loanSetFee));
        env(createJson);

        env.close();
    }

    // Under featureLendingProtocolV1_1, creating a LoanBroker on an
    // open-ended vault is rejected with tecNO_PERMISSION. With the amendment
    // disabled it succeeds. Updating an existing broker is unaffected.
    void
    testLoanBrokerRequiresClosedEndedVault()
    {
        testcase("LoanBrokerSet requires closed-ended vault under LP V1.1");
        using namespace jtx;

        Account const owner{"lp11_owner"};

        auto const build = [&](FeatureBitset features,
                               TER expected,
                               std::optional<TER> updateExpected = std::nullopt) {
            Env env(*this, features);
            env.fund(XRP(1'000), owner);
            env.close();

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = xrpIssue()});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = owner, .id = vaultKeylet.key, .amount = XRP(100)}));
            env.close();

            auto const brokerKeylet =
                keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
            env(loan_broker::set(owner, vaultKeylet.key), Ter(expected));
            env.close();

            // Updating an existing broker on the open-ended vault succeeds.
            if (updateExpected && expected == tesSUCCESS)
            {
                env(loan_broker::set(owner, vaultKeylet.key),
                    loan_broker::kLoanBrokerId(brokerKeylet.key),
                    loan_broker::kDebtMaximum(XRP(1'000).value()),
                    Ter(*updateExpected));
                env.close();
            }
        };

        // LP V1.1 disabled: broker create on an open-ended vault succeeds.
        build(all_, tesSUCCESS, tesSUCCESS);

        // LP V1.1 enabled: broker create on an open-ended vault is rejected.
        build(all_ | featureLendingProtocolV1_1, tecNO_PERMISSION);
    }

    void
    runAmendmentIndependent()
    {
        testDisabled();
        for (auto const kind : {VaultKind::OpenEnded, VaultKind::ClosedEnded})
            testInvalidLoanSet(kind);
        testLoanSetDoApplyPrecisionLoss();
        testInvalidLoanDelete();
        testInvalidLoanManage();
        testInvalidLoanAccept();
        testInvalidLoanPay();
        testTwoStepValidation();
        testRequireAuth();
        testLimitExceeded();
        testLoanBrokerRequiresClosedEndedVault();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testWrongMaxDebtBehavior(features);
    }

public:
    void
    run() override
    {
        runAmendmentIndependent();
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2}, all_))
            runAmendmentSensitive(features);
    }
};

BEAST_DEFINE_TESTSUITE(LoanValidation, tx, xrpl);

}  // namespace xrpl::test
