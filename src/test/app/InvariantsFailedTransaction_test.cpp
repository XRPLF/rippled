#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/offer.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ticket.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <source_location>
#include <string>
#include <vector>

namespace xrpl {

// Test-only factory — not part of the public API.
// The returned Transactor holds a raw reference to ctx; the caller must ensure
// the ApplyContext outlives the Transactor. Implemented in applySteps.cpp
std::unique_ptr<Transactor>
makeTransactor(ApplyContext& ctx);

}  // namespace xrpl

namespace xrpl::test {

/**
 * Tests for the `FailedTransaction` invariant (see InvariantCheck.h/.cpp).
 *
 * That invariant only does anything on a *failed* (non-`tesSUCCESS`)
 * transaction, so unlike the checks exercised in Invariants_test.cpp, the
 * invariant machinery has to be primed with a `tec` result code. The
 * `doInvariantCheck` helper below is a close relative of the one in
 * Invariants_test.cpp, but seeds the first pass with a caller-supplied `tec`
 * code instead of `tesSUCCESS`. It also always enables `featureTecInvariant`
 * (via testableAmendments), which is required: `FailedTransaction::finalize`
 * asserts that the amendment is enabled whenever it is about to return false.
 *
 * Every failure case is covered at least once. A "failure case" is either a
 * string appended to `errors_` in `visitEntry`, or a place where `result` is
 * set to false in `finalize`:
 *
 * visitEntry:
 *   - "Unexpected ledger entry created"          testUnexpectedEntryCreated
 *   - "<desc> was deleted"                       testAccountRootFeePayer,
 *                                                testSponsorFeePayer
 *   - "<desc> balance increased"                 testAccountRootFeePayer
 *   - "Multiple <desc>s were charged fees"       testAccountRootFeePayer
 *   - "Account root sequence decreased"          testAccountSequence
 *   - "Multiple Account root sequences ..."      testAccountSequence
 *   - "Account root sequence incremented by"     testAccountSequence
 *   - "Ticket was modified"                      testTicket
 *   - "Multiple tickets were deleted"            testTicket
 *   - "Unexpected ledger entry deleted"          testUnexpectedDeleteModify
 *   - "Unexpected ledger entry modified"         testUnexpectedDeleteModify
 *
 * finalize:
 *   - any collected error                        (all of the above)
 *   - "both account and sponsor paid fee"        testSponsorFeePayer
 *   - "both account sequence increased and       testSeqTicketCombinations
 *      ticket deleted"
 *   - "account sequence increased by a ticket    testSeqTicketCombinations
 *      transaction"
 *   - "ticket deleted by a sequence              testSeqTicketCombinations
 *      transaction"
 *   - "unexpected ledger entry deleted"          testDeletedObjects
 *   - "funded offer deleted"                     testDeletedObjects
 *   - "directory side effects without any        testDirectorySideEffects
 *      deleted objects"
 *
 * Three `errors_` sites in `visitEntry` are intentionally NOT covered because
 * they cannot be reached through the full invariant suite:
 *   - "before and after balances not comparable" (InvariantCheck.cpp) requires
 *     a fee-payer's balance to change currency. Both fee-payer balances
 *     (AccountRoot.Balance and Sponsorship.FeeAmount) are always XRP, so making
 *     them non-comparable means making one side non-XRP, which causes
 *     XRPNotCreated::visitEntry to throw from STAmount::xrp() before finalize
 *     ever runs, so the message can never be logged.
 *   - the `format == nullptr` branch is marked UNREACHABLE / LCOV_EXCL: an
 *     AccountRoot / Sponsorship always has a ledger format.
 *   - the "field modified" loop is dead code (guarded by an unconditional
 *     `break`).
 */
class InvariantsFailedTransaction_test : public beast::unit_test::Suite
{
    // Runs additional (valid) transactions on the ledger after the two funded
    // accounts are created, but before the ledger is closed and the precheck
    // runs. Used to place real objects (tickets, checks, offers, sponsorships)
    // into the ledger.
    using Preclose = std::function<
        bool(test::jtx::Account const& a, test::jtx::Account const& b, test::jtx::Env& env)>;

    // Manipulates the ApplyContext's view to simulate the changes a failing
    // transaction might (incorrectly) leave behind.
    using Precheck = std::function<
        bool(test::jtx::Account const& a, test::jtx::Account const& b, ApplyContext& ac)>;

    static FeatureBitset
    defaultAmendments()
    {
        // testableAmendments() includes featureTecInvariant and featureSponsor,
        // both of which are needed here.
        return xrpl::test::jtx::testableAmendments() | fixCleanup3_1_3 | fixCleanup3_2_0;
    }

    test::jtx::Env
    makeEnv()
    {
        return {
            *this, test::jtx::envconfig(), defaultAmendments(), nullptr, beast::Severity::Disabled};
    }

    /**
     * Run a single FailedTransaction invariant test case.
     *
     * @param expectLogs Messages that should appear in the log output.
     * @param preclose See "Preclose" above. Runs valid transactions to set up
     *  the ledger before the precheck. Use the overload without this parameter
     *  when no setup is needed.
     * @param precheck Manipulates the view to create the state to be detected.
     * @param startTer The `tec` code the simulated transaction "failed" with.
     *  This is fed into the first pass of the invariant checker. The invariant
     *  only inspects failed transactions, so this must be a `tec` (claim) code.
     * @param tx A mock transaction. Only its type and sequence/ticket fields
     *  matter to this invariant.
     * @param fee The fee the simulated transaction paid.
     */
    void
    doInvariantCheck(
        std::vector<std::string> const& expectLogs,
        Preclose const& preclose,
        Precheck const& precheck,
        TER const startTer,
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        XRPAmount const fee = XRPAmount{},
        std::source_location const& loc = std::source_location::current())
    {
        using namespace test::jtx;

        Env env = makeEnv();

        Account const a1{"A1"};
        Account const a2{"A2"};
        env.fund(XRP(1000), a1, a2);
        if (preclose)
            BEAST_EXPECT(preclose(a1, a2, env));
        env.close();

        OpenView ov{*env.current()};
        StreamSink sink{beast::Severity::Warning};
        beast::Journal const jlog{sink};
        ApplyContext ac{env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};

        // Invariants normally run in the Transaction's "apply" (operator())
        // context, and can always access global Rules.
        CurrentTransactionRulesGuard const rulesGuard(ov.rules());

        BEAST_EXPECT(precheck(a1, a2, ac));

        auto transactor = makeTransactor(ac);
        if (!BEAST_EXPECT(transactor))
            return;

        // The invariant checker is invoked twice, exactly as it would be for a
        // real transaction that claims a fee: the first pass sees the "real"
        // failure code and, upon detecting a broken invariant, returns
        // tecINVARIANT_FAILED. The second pass (charging only the fee) then
        // sees that and escalates to tefINVARIANT_FAILED.
        std::initializer_list<TER> const ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED};

        TER terActual = startTer;
        for (TER const& terExpect : ters)
        {
            terActual =
                transactor->checkInvariants(terActual, fee, Transactor::InvariantScope::Full);
            expect(
                terExpect == terActual,
                "expected: " + transToken(terExpect) + " got: " + transToken(terActual),
                loc.file_name(),
                loc.line());
            auto const messages = sink.messages().str();

            if (!isTesSuccess(terActual))
            {
                expect(
                    messages.starts_with("Invariant failed:") ||
                        messages.starts_with("Transaction caused an exception"),
                    messages,
                    loc.file_name(),
                    loc.line());
            }

            // std::cerr << messages << '\n';
            for (auto const& m : expectLogs)
            {
                expect(messages.contains(m), m, loc.file_name(), loc.line());
            }
        }
    }

    // Convenience overload for cases that need no `preclose` setup.
    void
    doInvariantCheck(
        std::vector<std::string> const& expectLogs,
        Precheck const& precheck,
        TER const startTer,
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        XRPAmount const fee = XRPAmount{},
        std::source_location const& loc = std::source_location::current())
    {
        doInvariantCheck(expectLogs, Preclose{}, precheck, startTer, std::move(tx), fee, loc);
    }

    void
    testUnexpectedEntryCreated()
    {
        using namespace test::jtx;
        testcase << "failed transaction created a ledger entry";

        // A failed transaction must not create any new ledger entries.
        doInvariantCheck(
            {{"Unexpected ledger entry created: Check"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // Insert a brand new (valid type) ledger entry.
                auto const sleNew =
                    std::make_shared<SLE>(keylet::check(a1.id(), SeqProxy::rawSequence(1)));
                ac.view().insert(sleNew);
                return true;
            },
            tecFAILED_PROCESSING);
    }

    void
    testAccountRootFeePayer()
    {
        using namespace test::jtx;
        testcase << "failed transaction account root fee payer";

        // The only account root balance change allowed by a failed transaction
        // is a single decrease (paying the fee).

        // An account root was deleted.
        doInvariantCheck(
            {{"Account root was deleted:"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            },
            tecFAILED_PROCESSING);

        // An account root's balance increased.
        doInvariantCheck(
            {{"Account root balance increased:"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                sle->setFieldAmount(sfBalance, sle->getFieldAmount(sfBalance) + STAmount{500});
                ac.view().update(sle);
                return true;
            },
            tecFAILED_PROCESSING);

        // More than one account root paid a fee (both balances decreased).
        doInvariantCheck(
            {{"Multiple Account roots were charged fees:"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                for (auto const& acct : {a1.id(), a2.id()})
                {
                    auto const sle = ac.view().peek(keylet::account(acct));
                    if (!sle)
                        return false;
                    sle->setFieldAmount(sfBalance, sle->getFieldAmount(sfBalance) - STAmount{500});
                    ac.view().update(sle);
                }
                return true;
            },
            tecFAILED_PROCESSING);
    }

    void
    testAccountSequence()
    {
        using namespace test::jtx;
        testcase << "failed transaction account sequence changes";

        // A failed transaction may only ever increment a single account root's
        // sequence, and by exactly one.

        // Sequence decreased.
        doInvariantCheck(
            {{"Account root sequence decreased:"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                sle->setFieldU32(sfSequence, sle->getFieldU32(sfSequence) - 1);
                ac.view().update(sle);
                return true;
            },
            tecFAILED_PROCESSING);

        // More than one account root's sequence was incremented.
        doInvariantCheck(
            {{"Multiple Account root sequences were incremented:"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                for (auto const& acct : {a1.id(), a2.id()})
                {
                    auto const sle = ac.view().peek(keylet::account(acct));
                    if (!sle)
                        return false;
                    sle->setFieldU32(sfSequence, sle->getFieldU32(sfSequence) + 1);
                    ac.view().update(sle);
                }
                return true;
            },
            tecFAILED_PROCESSING);

        // A single account root's sequence was incremented by more than one.
        doInvariantCheck(
            {{"Account root sequence incremented by 2:"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                sle->setFieldU32(sfSequence, sle->getFieldU32(sfSequence) + 2);
                ac.view().update(sle);
                return true;
            },
            tecFAILED_PROCESSING);
    }

    void
    testTicket()
    {
        using namespace test::jtx;
        testcase << "failed transaction ticket changes";

        // A failed transaction may delete at most one ticket, and may not
        // otherwise modify a ticket.

        // A ticket was modified (rather than only deleted).
        {
            std::uint32_t ticketSeq = 0;
            doInvariantCheck(
                {{"Ticket was modified:"}},
                [&ticketSeq](Account const& a1, Account const&, Env& env) {
                    ticketSeq = env.seq(a1) + 1;
                    env(ticket::create(a1, 1));
                    return true;
                },
                [&ticketSeq](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const sle =
                        ac.view().peek(keylet::ticket(a1.id(), SeqProxy::rawTicket(ticketSeq)));
                    if (!sle)
                        return false;
                    ac.view().update(sle);
                    return true;
                },
                tecFAILED_PROCESSING);
        }

        // More than one ticket was deleted.
        {
            std::uint32_t ticketSeq = 0;
            doInvariantCheck(
                {{"Multiple tickets were deleted:"}},
                [&ticketSeq](Account const& a1, Account const&, Env& env) {
                    ticketSeq = env.seq(a1) + 1;
                    env(ticket::create(a1, 2));
                    return true;
                },
                [&ticketSeq](Account const& a1, Account const&, ApplyContext& ac) {
                    for (std::uint32_t i = 0; i < 2; ++i)
                    {
                        auto const sle = ac.view().peek(
                            keylet::ticket(a1.id(), SeqProxy::rawTicket(ticketSeq + i)));
                        if (!sle)
                            return false;
                        ac.view().erase(sle);
                    }
                    return true;
                },
                tecFAILED_PROCESSING);
        }
    }

    void
    testUnexpectedDeleteModify()
    {
        using namespace test::jtx;
        testcase << "failed transaction touched an unexpected entry";

        auto const makeCheck = [](std::uint32_t& checkSeq) {
            return [&checkSeq](Account const& a1, Account const& a2, Env& env) {
                checkSeq = env.seq(a1);
                env(check::create(a1, a2, XRP(1)));
                return true;
            };
        };

        // An entry of an unexpected type was deleted. A failed transaction may
        // only delete tickets and (depending on the failure code) a small set
        // of expired objects; a Check is neither.
        {
            std::uint32_t checkSeq = 0;
            doInvariantCheck(
                {{"Unexpected ledger entry deleted: Check"}},
                makeCheck(checkSeq),
                [&checkSeq](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const sle =
                        ac.view().peek(keylet::check(a1.id(), SeqProxy::rawSequence(checkSeq)));
                    if (!sle)
                        return false;
                    ac.view().erase(sle);
                    return true;
                },
                tecFAILED_PROCESSING);
        }

        // An entry of an unexpected type was modified.
        {
            std::uint32_t checkSeq = 0;
            doInvariantCheck(
                {{"Unexpected ledger entry modified: Check"}},
                makeCheck(checkSeq),
                [&checkSeq](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const sle =
                        ac.view().peek(keylet::check(a1.id(), SeqProxy::rawSequence(checkSeq)));
                    if (!sle)
                        return false;
                    ac.view().update(sle);
                    return true;
                },
                tecFAILED_PROCESSING);
        }
    }

    void
    testSponsorFeePayer()
    {
        using namespace test::jtx;
        testcase << "failed transaction sponsor fee payer";

        // Create a sponsorship (a1 sponsors a2's fees) with a fee pool.
        auto const makeSponsorship = [](Account const& a1, Account const& a2, Env& env) {
            env(sponsor::set_fee(a1, 0, XRP(10), XRP(20)), sponsor::SponseeAcc(a2));
            return static_cast<bool>(env.le(keylet::sponsorship(a1.id(), a2.id())));
        };

        // A sponsorship was deleted. This exercises the shared fee-payer
        // "was deleted" path for the Sponsorship type.
        doInvariantCheck(
            {{"Sponsor was deleted:"}},
            makeSponsorship,
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::sponsorship(a1.id(), a2.id()));
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            },
            tecFAILED_PROCESSING);

        // Both an account root and a sponsorship paid a fee. Only one payer is
        // ever allowed.
        doInvariantCheck(
            {{"both account and sponsor paid fee"}},
            makeSponsorship,
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                // The account root pays a fee (balance decreases).
                auto const acct = ac.view().peek(keylet::account(a1.id()));
                if (!acct)
                    return false;
                acct->setFieldAmount(sfBalance, acct->getFieldAmount(sfBalance) - STAmount{500});
                ac.view().update(acct);

                // The sponsorship also pays a fee (its FeeAmount decreases).
                auto const spon = ac.view().peek(keylet::sponsorship(a1.id(), a2.id()));
                if (!spon || !spon->isFieldPresent(sfFeeAmount))
                    return false;
                spon->setFieldAmount(
                    sfFeeAmount, spon->getFieldAmount(sfFeeAmount) - STAmount{500});
                ac.view().update(spon);
                return true;
            },
            tecFAILED_PROCESSING);
    }

    void
    testSeqTicketCombinations()
    {
        using namespace test::jtx;
        testcase << "failed transaction sequence / ticket combinations";

        auto const bumpSequence = [](Account const& a1, Account const&, ApplyContext& ac) {
            auto const sle = ac.view().peek(keylet::account(a1.id()));
            if (!sle)
                return false;
            sle->setFieldU32(sfSequence, sle->getFieldU32(sfSequence) + 1);
            ac.view().update(sle);
            return true;
        };

        // A transaction may increment an account sequence OR delete a ticket,
        // but not both.
        {
            std::uint32_t ticketSeq = 0;
            doInvariantCheck(
                {{"both account sequence increased and ticket deleted"}},
                [&ticketSeq](Account const& a1, Account const&, Env& env) {
                    ticketSeq = env.seq(a1) + 1;
                    env(ticket::create(a1, 1));
                    return true;
                },
                [&ticketSeq, &bumpSequence](
                    Account const& a1, Account const& a2, ApplyContext& ac) {
                    if (!bumpSequence(a1, a2, ac))
                        return false;
                    auto const sle =
                        ac.view().peek(keylet::ticket(a1.id(), SeqProxy::rawTicket(ticketSeq)));
                    if (!sle)
                        return false;
                    ac.view().erase(sle);
                    return true;
                },
                tecFAILED_PROCESSING);
        }

        // A ticket transaction must not also increment the account sequence.
        doInvariantCheck(
            {{"account sequence increased by a ticket transaction"}},
            bumpSequence,
            tecFAILED_PROCESSING,
            STTx{ttACCOUNT_SET, [](STObject& tx) {
                     tx.setFieldU32(sfSequence, 0);
                     tx.setFieldU32(sfTicketSequence, 1);
                 }});

        // A sequence (non-ticket) transaction must not delete a ticket.
        {
            std::uint32_t ticketSeq = 0;
            doInvariantCheck(
                {{"ticket deleted by a sequence transaction"}},
                [&ticketSeq](Account const& a1, Account const&, Env& env) {
                    ticketSeq = env.seq(a1) + 1;
                    env(ticket::create(a1, 1));
                    return true;
                },
                [&ticketSeq](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const sle =
                        ac.view().peek(keylet::ticket(a1.id(), SeqProxy::rawTicket(ticketSeq)));
                    if (!sle)
                        return false;
                    ac.view().erase(sle);
                    return true;
                },
                tecFAILED_PROCESSING,
                STTx{ttACCOUNT_SET, [](STObject& tx) { tx.setFieldU32(sfSequence, 1); }});
        }
    }

    void
    testDeletedObjects()
    {
        using namespace test::jtx;
        testcase << "failed transaction deleted objects";

        auto const makeOffer = [](std::uint32_t& offerSeq) {
            return [&offerSeq](Account const& a1, Account const& a2, Env& env) {
                offerSeq = env.seq(a1);
                env(offer(a1, a2["USD"](10), XRP(10)));
                return static_cast<bool>(
                    env.le(keylet::offer(a1.id(), SeqProxy::rawSequence(offerSeq))));
            };
        };

        // An object was deleted that is not allowed to be deleted for this
        // failure code. tecEXPIRED allows deleting NFTokenOffers and
        // Credentials, but not Offers.
        {
            std::uint32_t offerSeq = 0;
            doInvariantCheck(
                {{"unexpected ledger entry deleted: Offer"}},
                makeOffer(offerSeq),
                [&offerSeq](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const sle =
                        ac.view().peek(keylet::offer(a1.id(), SeqProxy::rawSequence(offerSeq)));
                    if (!sle)
                        return false;
                    ac.view().erase(sle);
                    return true;
                },
                tecEXPIRED);
        }

        // A *funded* offer was deleted. tecKILLED allows deleting offers, but
        // only unfunded ones (where TakerPays is unchanged).
        {
            std::uint32_t offerSeq = 0;
            doInvariantCheck(
                {{"funded offer deleted: Offer"}},
                makeOffer(offerSeq),
                [&offerSeq](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle =
                        ac.view().peek(keylet::offer(a1.id(), SeqProxy::rawSequence(offerSeq)));
                    if (!sle)
                        return false;
                    // Partially consume the offer (change TakerPays) so it
                    // looks funded, then delete it.
                    sle->setFieldAmount(sfTakerPays, a2["USD"](5));
                    ac.view().update(sle);
                    ac.view().erase(sle);
                    return true;
                },
                tecKILLED);
        }
    }

    void
    testDirectorySideEffects()
    {
        using namespace test::jtx;
        testcase << "failed transaction directory side effects";

        // Directory modifications/deletions are only permitted as a side
        // effect of deleting expired objects. Touching a directory without
        // deleting any tracked object is not allowed.
        std::uint32_t checkSeq = 0;
        doInvariantCheck(
            {{"directory side effects without any deleted objects"}},
            [&checkSeq](Account const& a1, Account const& a2, Env& env) {
                // Give a1 an owner directory to touch.
                checkSeq = env.seq(a1);
                env(check::create(a1, a2, XRP(1)));
                return true;
            },
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::ownerDir(a1.id()));
                if (!sle)
                    return false;
                ac.view().update(sle);
                return true;
            },
            tecFAILED_PROCESSING);
    }

public:
    void
    run() override
    {
        testUnexpectedEntryCreated();
        testAccountRootFeePayer();
        testAccountSequence();
        testTicket();
        testUnexpectedDeleteModify();
        testSponsorFeePayer();
        testSeqTicketCombinations();
        testDeletedObjects();
        testDirectorySideEffects();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsFailedTransaction, app, xrpl);

}  // namespace xrpl::test
