#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <chrono>
#include <cstdint>
#include <optional>

namespace xrpl::test {

/**
 * CollateralLock: a LoanSet may pledge an existing token escrow with
 * `CollateralID`. While the loan is unpaid, EscrowFinish and EscrowCancel on
 * that escrow return tecPLEDGED, until the escrow's CancelAfter passes (the
 * pledge lapses). Paying the loan off releases it. Defaulting the loan sweeps
 * the escrowed amount to the loan broker's owner, as an EscrowFinish submitted
 * by that destination would.
 */
class CollateralLock_test : public LoanTestBase
{
    static constexpr std::uint32_t kTotal = 3;
    static constexpr std::uint32_t kInterval = 600;
    static constexpr std::uint32_t kGrace = 300;
    // Maturity (start + interval * total + grace) plus one more grace period:
    // the earliest CancelAfter a pledged escrow may have, relative to the
    // loan's start.
    static constexpr std::uint32_t kReleaseFloor = (kInterval * kTotal) + kGrace + kGrace;
    // Comfortably past the release floor.
    static constexpr std::chrono::seconds kCancelDelay{10'000};

    struct Fixture
    {
        jtx::Account issuer{"issuer"};
        jtx::Account lender{"lender"};
        jtx::Account borrower{"borrower"};
        jtx::Account evan{"evan"};
        std::optional<jtx::MPTTester> gold;
        std::optional<BrokerInfo> broker;
    };

    struct Options
    {
        // Whether the broker owner (lender) holds an MPToken for the collateral.
        bool lenderHoldsCollateral = true;
        // Whether the issuer requires authorisation, and has authorised the
        // lender.
        bool requireAuth = false;
        bool lenderAuthorized = true;
    };

    // Funds the actors, issues the collateral token (an MPT that can be
    // escrowed and transferred), gives the borrower 10,000 of it, and opens
    // an XRP vault and loan broker owned by the lender.
    void
    setup(jtx::Env& env, Fixture& f, Options const& o)
    {
        using namespace jtx;

        env.fund(XRP(100'000'000), f.issuer, noripple(f.lender, f.borrower, f.evan));
        env.close();

        std::uint32_t flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock;
        if (o.requireAuth)
            flags |= tfMPTRequireAuth;
        f.gold.emplace(env, f.issuer, MPTInit{.holders = {f.borrower, f.lender}, .fund = false});
        f.gold->create({.ownerCount = 1, .flags = flags});
        f.gold->authorize({.account = f.borrower});
        if (o.requireAuth)
            f.gold->authorize({.account = f.issuer, .holder = f.borrower});
        if (o.lenderHoldsCollateral)
        {
            f.gold->authorize({.account = f.lender});
            if (o.requireAuth && o.lenderAuthorized)
                f.gold->authorize({.account = f.issuer, .holder = f.lender});
        }
        env(pay(f.issuer, f.borrower, f.gold->operator()(10'000)));
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        f.broker.emplace(createVaultAndBroker(env, xrpAsset, f.lender));
    }

    void
    setup(jtx::Env& env, Fixture& f)
    {
        setup(env, f, Options{});
    }

    // A self-escrow of `amount` gold from the borrower, finishable once its
    // FinishAfter passes and cancellable after `cancelAfter`.
    Keylet
    pledgeableEscrow(
        jtx::Env& env,
        Fixture const& f,
        std::int64_t amount,
        std::chrono::seconds cancelAfter = kCancelDelay)
    {
        using namespace jtx;
        using namespace std::chrono_literals;

        auto const seq = env.seq(f.borrower);
        env(escrow::create(f.borrower, f.borrower, f.gold->operator()(amount)),
            escrow::kFinishTime(env.now() + 1s),
            escrow::kCancelTime(env.now() + cancelAfter),
            Fee(env.current()->fees().base * 150));
        env.close();
        return keylet::escrow(f.borrower.id(), SeqProxy::rawSequence(seq));
    }

    // A LoanSet from the borrower, countersigned by the broker owner,
    // pledging `collateral` when given.
    jtx::JTx
    loanSet(jtx::Env& env, Fixture const& f, std::optional<uint256> const& collateral)
    {
        using namespace jtx;
        using namespace jtx::loan;

        JTx jt{set(f.borrower, f.broker->brokerID, f.broker->asset(1'000).number())};
        kCounterparty(f.lender)(env, jt);
        kPaymentTotal(kTotal)(env, jt);
        kPaymentInterval(kInterval)(env, jt);
        kGracePeriod(kGrace)(env, jt);
        if (collateral)
            jt.jv[sfCollateralID.jsonName] = to_string(*collateral);
        Sig(sfCounterpartySignature, f.lender)(env, jt);
        Fee{env.current()->fees().base * 5}(env, jt);
        return jt;
    }

    void
    payOff(jtx::Env& env, Fixture const& f, Keylet const& loanKeylet)
    {
        using namespace jtx;

        auto const state = getCurrentState(env, *f.broker, loanKeylet);
        STAmount const payment{
            f.broker->asset,
            roundPeriodicPayment(f.broker->asset, state.periodicPayment, state.loanScale)};
        for (std::uint32_t i = 0; i < state.paymentRemaining; ++i)
        {
            env(loan::pay(f.borrower, loanKeylet.key, payment));
            env.close();
        }
        if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            BEAST_EXPECT(loanSle->at(sfPaymentRemaining) == 0);
    }

    // Advances the ledger close time past the loan's first due date plus
    // grace, so the broker may default it.
    void
    pastDefaultTime(jtx::Env& env, Keylet const& loanKeylet)
    {
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        std::uint32_t const due = loanSle->at(sfNextPaymentDueDate);
        env.close(NetClock::time_point{NetClock::duration{due + kGrace + 1}});
    }

    // Env::close(t) adds one close time resolution and rounds to the nearest
    // multiple of it, so asking for (m - resolution) closes exactly at m. This
    // returns the request that closes at the latest multiple at or before t.
    static NetClock::time_point
    closeAtOrBefore(jtx::Env& env, std::uint32_t t)
    {
        auto const res =
            static_cast<std::uint32_t>(env.closed()->header().closeTimeResolution.count());
        return NetClock::time_point{NetClock::duration{t - (t % res) - res}};
    }

    static std::uint32_t
    ownerCount(jtx::Env& env, jtx::Account const& a)
    {
        auto const sle = env.le(keylet::account(a.id()));
        return sle ? sle->at(sfOwnerCount) : 0;
    }

    void
    testDisabled()
    {
        testcase("Disabled");
        using namespace jtx;

        Env env(*this, all_ - featureCollateralLock);
        Fixture f;
        setup(env, f);
        auto const escrow = pledgeableEscrow(env, f, 1'000);

        // CollateralID is rejected without the amendment...
        env(loanSet(env, f, escrow.key), Ter(temDISABLED));
        // ...and a LoanSet without it behaves as before.
        env(loanSet(env, f, std::nullopt));
        env.close();
    }

    void
    testNoCollateralUnchanged()
    {
        testcase("Loan without CollateralID is unchanged");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, all_);
        Fixture f;
        setup(env, f);
        auto const escrow = pledgeableEscrow(env, f, 1'000);
        auto const loanKeylet = nextLoanKeylet(env, *f.broker);

        env(loanSet(env, f, std::nullopt));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        auto const escrowSle = env.le(escrow);
        if (!BEAST_EXPECT(loanSle && escrowSle))
            return;
        BEAST_EXPECT(!loanSle->isFieldPresent(sfCollateralID));
        BEAST_EXPECT(!escrowSle->isFieldPresent(sfLoanID));

        // The escrow knows nothing about the loan, exactly as today.
        env.close(env.now() + 2s);
        env(escrow::finish(f.borrower, f.borrower, escrowSle->getFieldU32(sfSequence)),
            Fee(env.current()->fees().base * 150));
        env.close();
        BEAST_EXPECT(!env.le(escrow));
    }

    void
    testPreclaim()
    {
        testcase("LoanSet preclaim");
        using namespace jtx;
        using namespace std::chrono_literals;

        {
            Env env(*this, all_);
            Fixture f;
            setup(env, f);

            // No such escrow.
            env(loanSet(env, f, uint256{1}), Ter(tecNO_ENTRY));

            // An escrow owned by someone else.
            env(pay(f.issuer, f.lender, f.gold->operator()(1'000)));
            auto const seq = env.seq(f.lender);
            env(escrow::create(f.lender, f.lender, f.gold->operator()(500)),
                escrow::kFinishTime(env.now() + 1s),
                escrow::kCancelTime(env.now() + kCancelDelay),
                Fee(env.current()->fees().base * 150));
            env.close();
            env(loanSet(env, f, keylet::escrow(f.lender.id(), SeqProxy::rawSequence(seq)).key),
                Ter(tecNO_PERMISSION));

            // An escrow payable to someone other than the borrower.
            auto const seq2 = env.seq(f.borrower);
            env(escrow::create(f.borrower, f.lender, f.gold->operator()(500)),
                escrow::kFinishTime(env.now() + 1s),
                escrow::kCancelTime(env.now() + kCancelDelay),
                Fee(env.current()->fees().base * 150));
            env.close();
            env(loanSet(env, f, keylet::escrow(f.borrower.id(), SeqProxy::rawSequence(seq2)).key),
                Ter(tecNO_PERMISSION));

            // An XRP escrow is not token collateral.
            auto const seq3 = env.seq(f.borrower);
            env(escrow::create(f.borrower, f.borrower, XRP(10)),
                escrow::kFinishTime(env.now() + 1s),
                escrow::kCancelTime(env.now() + kCancelDelay));
            env.close();
            env(loanSet(env, f, keylet::escrow(f.borrower.id(), SeqProxy::rawSequence(seq3)).key),
                Ter(tecWRONG_ASSET));

            // Cancellable before the release floor.
            auto const early = pledgeableEscrow(env, f, 1'000, 1'000s);
            env(loanSet(env, f, early.key), Ter(tecTOO_SOON));

            // No CancelAfter at all: the pledge could never lapse.
            auto const seq4 = env.seq(f.borrower);
            env(escrow::create(f.borrower, f.borrower, f.gold->operator()(500)),
                escrow::kFinishTime(env.now() + 1s),
                Fee(env.current()->fees().base * 150));
            env.close();
            env(loanSet(env, f, keylet::escrow(f.borrower.id(), SeqProxy::rawSequence(seq4)).key),
                Ter(tecTOO_SOON));

            // Already pledged to another loan.
            auto const escrow = pledgeableEscrow(env, f, 1'000);
            env(loanSet(env, f, escrow.key));
            env.close();
            env(loanSet(env, f, escrow.key), Ter(tecDUPLICATE));
        }

        {
            // The issuer requires authorisation and has not authorised the
            // broker owner, so it could not receive the collateral.
            Env env(*this, all_);
            Fixture f;
            setup(env, f, {.requireAuth = true, .lenderAuthorized = false});
            auto const escrow = pledgeableEscrow(env, f, 1'000);
            env(loanSet(env, f, escrow.key), Ter(tecNO_AUTH));
        }
    }

    void
    testReleaseFloor()
    {
        testcase("CancelAfter must reach maturity plus one grace period");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, all_);
        Fixture f;
        setup(env, f);

        // The loan starts at the parent close time of the ledger the LoanSet
        // is applied in, so close the ledger at a chosen time before it.
        auto const pledgeStartingAt = [&](Keylet const& escrow, std::uint32_t start) {
            env.close(closeAtOrBefore(env, start));
            return loanSet(env, f, escrow.key);
        };

        // Self-escrows whose CancelAfter is a time Env::close can reach exactly.
        auto const alignedEscrow = [&](std::uint32_t offset) {
            auto const seq = env.seq(f.borrower);
            auto const when = closeAtOrBefore(
                env, static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + offset);
            env(escrow::create(f.borrower, f.borrower, f.gold->operator()(1'000)),
                escrow::kFinishTime(env.now() + 1s),
                escrow::kCancelTime(when),
                Fee(env.current()->fees().base * 150));
            env.close();
            return keylet::escrow(f.borrower.id(), SeqProxy::rawSequence(seq));
        };
        auto const exact = alignedEscrow(10'000);
        std::uint32_t const cancelAfter = env.le(exact)->at(sfCancelAfter);
        // One grace period short of the floor.
        auto const shortEscrow = alignedEscrow(20'000);
        std::uint32_t const shortCancelAfter = env.le(shortEscrow)->at(sfCancelAfter);

        // The loan's StartDate is the close time of the ledger the LoanSet is
        // applied in, one resolution step after the open ledger's parent.
        auto const res =
            static_cast<std::uint32_t>(env.closed()->header().closeTimeResolution.count());

        // Exactly at the floor: the closing ledger's time makes the floor equal
        // CancelAfter.
        env(pledgeStartingAt(exact, cancelAfter - kReleaseFloor - res));
        env.close();
        if (auto const sle = env.le(exact); BEAST_EXPECT(sle))
            BEAST_EXPECT(sle->isFieldPresent(sfLoanID));

        // One grace period short of the floor.
        env(pledgeStartingAt(shortEscrow, shortCancelAfter - kReleaseFloor + kGrace),
            Ter(tecTOO_SOON));
        env.close();
        if (auto const sle = env.le(shortEscrow); BEAST_EXPECT(sle))
            BEAST_EXPECT(!sle->isFieldPresent(sfLoanID));
    }

    void
    testPledgedUntilPaid()
    {
        testcase("Finish and Cancel return tecPLEDGED until payoff");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, all_);
        Fixture f;
        setup(env, f);
        auto const escrow = pledgeableEscrow(env, f, 1'000);
        auto const escrowSeq = env.le(escrow)->getFieldU32(sfSequence);
        auto const loanKeylet = nextLoanKeylet(env, *f.broker);

        env(loanSet(env, f, escrow.key));
        env.close();

        // The loan and the escrow point at each other.
        auto const loanSle = env.le(loanKeylet);
        auto const escrowSle = env.le(escrow);
        if (!BEAST_EXPECT(loanSle && escrowSle))
            return;
        BEAST_EXPECT(loanSle->getFieldH256(sfCollateralID) == escrow.key);
        BEAST_EXPECT(escrowSle->getFieldH256(sfLoanID) == loanKeylet.key);

        env.close(env.now() + 2s);
        auto const fee = Fee(env.current()->fees().base * 150);

        // Nobody can finish the pledged escrow while the loan is open: not the
        // borrower who owns it, not the broker owner, not a stranger.
        env(escrow::finish(f.borrower, f.borrower, escrowSeq), fee, Ter(tecPLEDGED));
        env(escrow::finish(f.lender, f.borrower, escrowSeq), fee, Ter(tecPLEDGED));
        env(escrow::finish(f.evan, f.borrower, escrowSeq), fee, Ter(tecPLEDGED));

        env(escrow::cancel(f.borrower, f.borrower, escrowSeq), fee, Ter(tecPLEDGED));
        env.close();
        BEAST_EXPECT(env.le(escrow));
        BEAST_EXPECT(f.gold->getBalance(f.borrower) == 9'000);

        // Pay the loan off; the same EscrowFinish now succeeds.
        payOff(env, f, loanKeylet);
        env(escrow::finish(f.borrower, f.borrower, escrowSeq), fee);
        env.close();
        BEAST_EXPECT(!env.le(escrow));
        BEAST_EXPECT(f.gold->getBalance(f.borrower) == 10'000);
    }

    void
    testCancelAfterPayoffAndDelete()
    {
        testcase("EscrowCancel succeeds after payoff and LoanDelete");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, all_);
        Fixture f;
        setup(env, f);
        auto const escrow = pledgeableEscrow(env, f, 1'000);
        auto const escrowSeq = env.le(escrow)->getFieldU32(sfSequence);
        auto const loanKeylet = nextLoanKeylet(env, *f.broker);

        env(loanSet(env, f, escrow.key));
        env.close(env.now() + 2s);
        auto const fee = Fee(env.current()->fees().base * 150);
        env(escrow::finish(f.borrower, f.borrower, escrowSeq), fee, Ter(tecPLEDGED));

        payOff(env, f, loanKeylet);
        // The loan can be deleted after payoff; the escrow stays released.
        env(loan::del(f.borrower, loanKeylet.key));
        env.close();
        BEAST_EXPECT(!env.le(loanKeylet));

        env.close(env.now() + kCancelDelay);
        env(escrow::cancel(f.borrower, f.borrower, escrowSeq), fee);
        env.close();
        BEAST_EXPECT(!env.le(escrow));
        BEAST_EXPECT(f.gold->getBalance(f.borrower) == 10'000);
    }

    void
    testDefaultSweep()
    {
        testcase("Default sweeps the collateral to the broker owner");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, all_);
        Fixture f;
        setup(env, f);
        auto const escrow = pledgeableEscrow(env, f, 1'000);
        auto const escrowSeq = env.le(escrow)->getFieldU32(sfSequence);
        auto const loanKeylet = nextLoanKeylet(env, *f.broker);

        env(loanSet(env, f, escrow.key));
        env.close();

        auto const borrowerOwners = ownerCount(env, f.borrower);
        auto const lenderGold = f.gold->getBalance(f.lender);

        // Too soon: the loan isn't past due plus grace yet.
        env(loan::manage(f.lender, loanKeylet.key, tfLoanDefault), Ter(tecTOO_SOON));

        // Past due plus grace, before CancelAfter: the borrower can't take the
        // collateral back.
        pastDefaultTime(env, loanKeylet);
        auto const fee = Fee(env.current()->fees().base * 150);
        env(escrow::cancel(f.borrower, f.borrower, escrowSeq), fee, Ter(tecPLEDGED));
        env(escrow::finish(f.borrower, f.borrower, escrowSeq), fee, Ter(tecPLEDGED));
        env.close();

        env(loan::manage(f.lender, loanKeylet.key, tfLoanDefault));
        env.close();

        // The escrow is gone and the broker owner holds the collateral, in the
        // same transaction that defaulted the loan.
        BEAST_EXPECT(!env.le(escrow));
        BEAST_EXPECT(f.gold->getBalance(f.lender) == lenderGold + 1'000);
        BEAST_EXPECT(f.gold->getBalance(f.borrower) == 9'000);
        BEAST_EXPECT(ownerCount(env, f.borrower) == borrowerOwners - 1);
        if (auto const after = env.le(loanKeylet); BEAST_EXPECT(after))
            BEAST_EXPECT(after->isFlag(lsfLoanDefault));

        // Nothing left to finish.
        env(escrow::finish(f.borrower, f.borrower, escrowSeq), fee, Ter(tecNO_TARGET));
    }

    void
    testDefaultBlockedByLock()
    {
        testcase("Default fails, and the pledge stays, if the broker owner is locked");
        using namespace jtx;

        Env env(*this, all_);
        Fixture f;
        setup(env, f);
        auto const escrow = pledgeableEscrow(env, f, 1'000);
        auto const loanKeylet = nextLoanKeylet(env, *f.broker);

        env(loanSet(env, f, escrow.key));
        env.close();

        // The issuer locks the broker owner's collateral holding.
        f.gold->set({.holder = f.lender, .flags = tfMPTLock});
        pastDefaultTime(env, loanKeylet);

        env(loan::manage(f.lender, loanKeylet.key, tfLoanDefault), Ter(tecLOCKED));
        env.close();
        BEAST_EXPECT(env.le(escrow));
        if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            BEAST_EXPECT(!loanSle->isFlag(lsfLoanDefault));

        // Once the lock is lifted, the default goes through.
        f.gold->set({.holder = f.lender, .flags = tfMPTUnlock});
        env(loan::manage(f.lender, loanKeylet.key, tfLoanDefault));
        env.close();
        BEAST_EXPECT(!env.le(escrow));
    }

    void
    testPledgeLapses()
    {
        testcase("The pledge lapses at CancelAfter when the broker never defaults");
        using namespace jtx;

        {
            Env env(*this, all_);
            Fixture f;
            setup(env, f);
            auto const escrow = pledgeableEscrow(env, f, 1'000);
            auto const escrowSeq = env.le(escrow)->getFieldU32(sfSequence);
            auto const loanKeylet = nextLoanKeylet(env, *f.broker);
            env(loanSet(env, f, escrow.key));
            env.close();
            std::uint32_t const cancelAfter = env.le(escrow)->at(sfCancelAfter);
            auto const fee = Fee(env.current()->fees().base * 150);
            auto const lenderGold = f.gold->getBalance(f.lender);

            // At (or just before) CancelAfter the pledge still holds.
            env.close(closeAtOrBefore(env, cancelAfter));
            env(escrow::cancel(f.borrower, f.borrower, escrowSeq), fee, Ter(tecPLEDGED));
            env.close(NetClock::time_point{NetClock::duration{cancelAfter + 1}});

            // Once it has passed, the escrow follows the ordinary XLS-85 rules:
            // it can no longer be finished, and the borrower can cancel it even
            // though the loan is unpaid.
            env(escrow::finish(f.borrower, f.borrower, escrowSeq), fee, Ter(tecNO_PERMISSION));
            env(escrow::cancel(f.borrower, f.borrower, escrowSeq), fee);
            env.close();
            BEAST_EXPECT(!env.le(escrow));
            BEAST_EXPECT(f.gold->getBalance(f.borrower) == 10'000);
            if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            {
                BEAST_EXPECT(loanSle->at(sfPaymentRemaining) == kTotal);
                BEAST_EXPECT(loanSle->getFieldH256(sfCollateralID) == escrow.key);
            }

            // The broker can still default the loan for the cover and vault
            // accounting; there is no collateral left to sweep.
            env(loan::manage(f.lender, loanKeylet.key, tfLoanDefault));
            env.close();
            BEAST_EXPECT(f.gold->getBalance(f.lender) == lenderGold);
            if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
                BEAST_EXPECT(loanSle->isFlag(lsfLoanDefault));
        }

        {
            // After the lapse, the lender keeps its claim until the borrower
            // actually cancels: a default still sweeps the escrow.
            Env env(*this, all_);
            Fixture f;
            setup(env, f);
            auto const escrow = pledgeableEscrow(env, f, 1'000);
            auto const loanKeylet = nextLoanKeylet(env, *f.broker);
            env(loanSet(env, f, escrow.key));
            env.close();
            std::uint32_t const cancelAfter = env.le(escrow)->at(sfCancelAfter);
            auto const lenderGold = f.gold->getBalance(f.lender);

            env.close(NetClock::time_point{NetClock::duration{cancelAfter + 1}});
            env(loan::manage(f.lender, loanKeylet.key, tfLoanDefault));
            env.close();
            BEAST_EXPECT(!env.le(escrow));
            BEAST_EXPECT(f.gold->getBalance(f.lender) == lenderGold + 1'000);
        }
    }

    void
    testSweepAsEscrowFinish()
    {
        testcase("The sweep behaves as an EscrowFinish by the broker owner");
        using namespace jtx;

        {
            // The broker owner holds no MPToken: the pledge is accepted and the
            // sweep creates the holding, as the destination's own EscrowFinish
            // would.
            Env env(*this, all_);
            Fixture f;
            setup(env, f, {.lenderHoldsCollateral = false});
            auto const escrow = pledgeableEscrow(env, f, 1'000);
            auto const loanKeylet = nextLoanKeylet(env, *f.broker);
            env(loanSet(env, f, escrow.key));
            env.close();

            auto const lenderOwners = ownerCount(env, f.lender);
            pastDefaultTime(env, loanKeylet);
            env(loan::manage(f.lender, loanKeylet.key, tfLoanDefault));
            env.close();
            BEAST_EXPECT(!env.le(escrow));
            BEAST_EXPECT(f.gold->getBalance(f.lender) == 1'000);
            BEAST_EXPECT(ownerCount(env, f.lender) == lenderOwners + 1);
        }

        {
            // A locked borrower does not block the lender's recovery: the
            // escrowed funds are held by the issuer, not the borrower.
            Env env(*this, all_);
            Fixture f;
            setup(env, f);
            auto const escrow = pledgeableEscrow(env, f, 1'000);
            auto const loanKeylet = nextLoanKeylet(env, *f.broker);
            env(loanSet(env, f, escrow.key));
            env.close();

            f.gold->set({.holder = f.borrower, .flags = tfMPTLock});
            auto const lenderGold = f.gold->getBalance(f.lender);
            pastDefaultTime(env, loanKeylet);
            env(loan::manage(f.lender, loanKeylet.key, tfLoanDefault));
            env.close();
            BEAST_EXPECT(!env.le(escrow));
            BEAST_EXPECT(f.gold->getBalance(f.lender) == lenderGold + 1'000);
        }
    }

    void
    testIOUCollateral()
    {
        testcase("IOU collateral");
        using namespace jtx;
        using namespace std::chrono_literals;

        // Trust line collateral against an XRP loan. `lenderLimit` is the
        // broker owner's trust line limit, or none for no trust line.
        auto const run = [&](std::optional<std::int64_t> lenderLimit, bool deepFreezeLender) {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};
            auto const usd = issuer["USD"];
            env.fund(XRP(100'000'000), issuer, noripple(lender, borrower));
            env(fset(issuer, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), borrower);
            if (lenderLimit)
                env.trust(usd(*lenderLimit), lender);
            env.close();
            env(pay(issuer, borrower, usd(10'000)));
            env.close();

            Fixture f;
            f.broker.emplace(createVaultAndBroker(env, PrettyAsset{xrpIssue(), 1'000'000}, lender));

            auto const seq = env.seq(borrower);
            env(escrow::create(borrower, borrower, usd(1'000)),
                escrow::kFinishTime(env.now() + 1s),
                escrow::kCancelTime(env.now() + kCancelDelay),
                Fee(env.current()->fees().base * 150));
            env.close();
            auto const escrow = keylet::escrow(borrower.id(), SeqProxy::rawSequence(seq));
            auto const loanKeylet = nextLoanKeylet(env, *f.broker);

            JTx jt{loan::set(borrower, f.broker->brokerID, f.broker->asset(1'000).number())};
            loan::kCounterparty(lender)(env, jt);
            loan::kPaymentTotal(kTotal)(env, jt);
            loan::kPaymentInterval(kInterval)(env, jt);
            loan::kGracePeriod(kGrace)(env, jt);
            jt.jv[sfCollateralID.jsonName] = to_string(escrow.key);
            Sig(sfCounterpartySignature, lender)(env, jt);
            Fee{env.current()->fees().base * 5}(env, jt);
            env(jt);
            env.close();
            BEAST_EXPECT(env.le(escrow)->isFieldPresent(sfLoanID));

            if (deepFreezeLender)
                env(trust(issuer, usd(0), lender, tfSetFreeze | tfSetDeepFreeze));

            auto const lenderOwners = ownerCount(env, lender);
            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return;
            std::uint32_t const due = loanSle->at(sfNextPaymentDueDate);
            env.close(NetClock::time_point{NetClock::duration{due + kGrace + 1}});

            if (deepFreezeLender)
            {
                env(loan::manage(lender, loanKeylet.key, tfLoanDefault), Ter(tecFROZEN));
                env.close();
                BEAST_EXPECT(env.le(escrow));
                return;
            }

            env(loan::manage(lender, loanKeylet.key, tfLoanDefault));
            env.close();
            BEAST_EXPECT(!env.le(escrow));
            BEAST_EXPECT(env.balance(lender, usd) == usd(1'000));
            BEAST_EXPECT(env.balance(borrower, usd) == usd(9'000));
            if (!lenderLimit)
                BEAST_EXPECT(ownerCount(env, lender) == lenderOwners + 1);
        };

        // The broker owner's line has room.
        run(100'000, false);
        // The line exists but its limit is too low: the broker owner signed the
        // LoanManage, so, as for an EscrowFinish by the destination, the limit is
        // not checked.
        run(500, false);
        // No line yet: the sweep creates it.
        run(std::nullopt, false);
        // The issuer deep-freezes the broker owner: the default fails and the
        // escrow stays pledged.
        run(100'000, true);
    }

public:
    void
    run() override
    {
        testDisabled();
        testNoCollateralUnchanged();
        testPreclaim();
        testReleaseFloor();
        testPledgedUntilPaid();
        testCancelAfterPayoffAndDelete();
        testDefaultSweep();
        testDefaultBlockedByLock();
        testPledgeLapses();
        testSweepAsEscrowFinish();
        testIOUCollateral();
    }
};

BEAST_DEFINE_TESTSUITE(CollateralLock, tx, xrpl);

}  // namespace xrpl::test
