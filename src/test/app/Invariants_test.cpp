#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/invariants/AMMInvariant.h>
#include <xrpl/tx/invariants/DirectoryInvariant.h>
#include <xrpl/tx/invariants/InvariantRunner.h>
#include <xrpl/tx/invariants/PermissionedDEXInvariant.h>
#include <xrpl/tx/invariants/VaultInvariant.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

// Test-only factory — not part of the public API.
// The returned Transactor holds a raw reference to ctx; the caller must ensure
// the ApplyContext outlives the Transactor. Implemented in applySteps.cpp
std::unique_ptr<Transactor>
makeTransactor(ApplyContext& ctx);

}  // namespace xrpl

namespace xrpl::test {

class Invariants_test : public beast::unit_test::Suite
{
    // The optional Preclose function is used to process additional transactions
    // on the ledger after creating two accounts, but before closing it, and
    // before the Precheck function. These should only be valid functions, and
    // not direct manipulations. Preclose is not commonly used.
    using Preclose = std::function<
        bool(test::jtx::Account const& a, test::jtx::Account const& b, test::jtx::Env& env)>;

    // this is common setup/method for running a failing invariant check. The
    // precheck function is used to manipulate the ApplyContext with view
    // changes that will cause the check to fail.
    using Precheck = std::function<
        bool(test::jtx::Account const& a, test::jtx::Account const& b, ApplyContext& ac)>;

    static FeatureBitset
    defaultAmendments()
    {
        return xrpl::test::jtx::testableAmendments() | fixCleanup3_1_3 | fixCleanup3_2_0;
    }

    test::jtx::Env
    makeEnv(FeatureBitset features)
    {
        return {*this, test::jtx::envconfig(), features, nullptr, beast::Severity::Disabled};
    }

    /**
     * Run a specific test case to put the ledger into a state that will be
     * detected by an invariant. Simulates the actions of a transaction that
     * would violate an invariant.
     *
     * @param expect_logs One or more messages related to the failing invariant
     *  that should be in the log output
     * @precheck See "Precheck" above
     * @fee If provided, the fee amount paid by the simulated transaction.
     * @tx A mock transaction that took the actions to trigger the invariant. In
     *  most cases, only the type matters.
     * @ters The TER results expected on the two passes of the invariant
     *  checker.
     * @preclose See "Preclose" above. Note that @preclose runs *before*
     * @precheck, but is the last parameter for historical reasons
     * @setTxAccount optionally set to add sfAccount to tx (either A1 or A2)
     */
    enum class TxAccount : int { None = 0, A1, A2 };
    void
    doInvariantCheck(
        std::vector<std::string> const& expectLogs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        Preclose const& preclose = {},
        TxAccount setTxAccount = TxAccount::None,
        std::source_location const& loc = std::source_location::current(),
        // Result fed to the invariant checker on the first pass. Set it to a
        // tec to exercise result-dependent invariants; the harness runs no
        // transactor, so one never arises on its own.
        TER initialResult = tesSUCCESS)
    {
        doInvariantCheck(
            makeEnv(defaultAmendments()),
            expectLogs,
            precheck,
            fee,
            tx,
            ters,
            preclose,
            setTxAccount,
            loc,
            initialResult);
    }

    void
    doInvariantCheck(
        test::jtx::Env&& env,
        std::vector<std::string> const& expectLogs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        Preclose const& preclose = {},
        TxAccount setTxAccount = TxAccount::None,
        std::source_location const& loc = std::source_location::current(),
        TER initialResult = tesSUCCESS)
    {
        using namespace test::jtx;

        Account const a1{"A1"};
        Account const a2{"A2"};
        env.fund(XRP(1000), a1, a2);
        if (preclose)
            expect(preclose(a1, a2, env), "preclose(a1, a2, env)", loc.file_name(), loc.line());
        env.close();

        if (setTxAccount != TxAccount::None)
            tx.setAccountID(sfAccount, setTxAccount == TxAccount::A1 ? a1.id() : a2.id());

        doInvariantCheck(
            std::move(env), a1, a2, expectLogs, precheck, fee, tx, ters, loc, initialResult);
    }

    void
    doInvariantCheck(
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
        test::jtx::Env&& env,
        test::jtx::Account const& a1,
        test::jtx::Account const& a2,
        std::vector<std::string> const& expectLogs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        std::source_location const& loc = std::source_location::current(),
        TER initialResult = tesSUCCESS)
    {
        using namespace test::jtx;

        OpenView ov{*env.current()};
        test::StreamSink sink{beast::Severity::Warning};
        beast::Journal const jlog{sink};
        ApplyContext ac{env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};

        // Invariants normally run in the Transaction's "apply" (operator()) context, and can always
        // access global Rules.
        CurrentTransactionRulesGuard const rulesGuard(ov.rules());

        expect(precheck(a1, a2, ac), loc.file_name(), loc.line());

        auto transactor = makeTransactor(ac);
        if (!expect(transactor, loc.file_name(), loc.line()))
            return;

        // Invoke the check twice to cover the tec and tef cases. Both passes run
        // against the same view -- production would discard it in between -- so
        // the second sees the same violation and escalates tec -> tef. A
        // {tec, tef} pair therefore means "enforced whatever the incoming
        // result", not that the transaction ends in tef on ledger.
        if (!expect(ters.size() == 2, loc.file_name(), loc.line()))
            return;

        TER terActual = initialResult;
        for (TER const& terExpect : ters)
        {
            TER const terInput = terActual;
            terActual =
                transactor->checkInvariants(terActual, fee, Transactor::InvariantScope::Full);
            expect(
                terExpect == terActual,
                "expected: " + transToken(terExpect) + " got: " + transToken(terActual),
                loc.file_name(),
                loc.line());
            auto const messages = sink.messages().str();

            // checkInvariants returns its input unchanged unless something
            // fires, so a changed result means an invariant fired, and a firing
            // invariant must log.
            if (terActual != terInput)
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

    void
    testXRPNotCreated()
    {
        using namespace test::jtx;
        testcase << "XRP created";
        doInvariantCheck(
            {{"XRP net change was positive: 500"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // put a single account in the view and "manufacture" some XRP
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto amt = sle->getFieldAmount(sfBalance);
                sle->setFieldAmount(sfBalance, amt + STAmount{500});
                ac.view().update(sle);
                return true;
            });
    }

    void
    testAccountRootsNotRemoved()
    {
        using namespace test::jtx;
        testcase << "account root removed";

        // An account was deleted, but not by an AccountDelete transaction.
        doInvariantCheck(
            {{"an account root was deleted"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // remove an account from the view
                auto sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                // Clear the balance so the "account deletion left behind a
                // non-zero balance" check doesn't trip earlier than the desired
                // check.
                sle->at(sfBalance) = beast::kZero;
                ac.view().erase(sle);
                return true;
            });

        // Successful AccountDelete transaction that didn't delete an account.
        //
        // Note that this is a case where a second invocation of the invariant
        // checker returns a tecINVARIANT_FAILED, not a tefINVARIANT_FAILED.
        // After a discussion with the team, we believe that's okay.
        doInvariantCheck(
            {{"account deletion succeeded without deleting an account"}},
            [](Account const&, Account const&, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // Successful AccountDelete that deleted more than one account.
        doInvariantCheck(
            {{"account deletion succeeded but deleted multiple accounts"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                // remove two accounts from the view
                auto sleA1 = ac.view().peek(keylet::account(a1.id()));
                auto sleA2 = ac.view().peek(keylet::account(a2.id()));
                if (!sleA1 || !sleA2)
                    return false;
                // Clear the balance so the "account deletion left behind a
                // non-zero balance" check doesn't trip earlier than the desired
                // check.
                sleA1->at(sfBalance) = beast::kZero;
                sleA2->at(sfBalance) = beast::kZero;
                ac.view().erase(sleA1);
                ac.view().erase(sleA2);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});
    }

    void
    testAccountRootsDeletedClean()
    {
        using namespace test::jtx;
        testcase << "account root deletion left artifact";

        doInvariantCheck(
            {{"account deletion left behind a non-zero balance"}},
            // NOLINTNEXTLINE(readability-identifier-naming)
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                // A1 has a balance. Delete A1
                auto const a1 = A1.id();
                auto const sleA1 = ac.view().peek(keylet::account(a1));
                if (!sleA1)
                    return false;
                if (!BEAST_EXPECT(*sleA1->at(sfBalance) != beast::kZero))
                    return false;

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"account deletion left behind a non-zero owner count"}},
            // NOLINTNEXTLINE(readability-identifier-naming)
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                // Increment A1's owner count, then delete A1
                auto const a1 = A1.id();
                auto const sleA1 = ac.view().peek(keylet::account(a1));
                if (!sleA1)
                    return false;
                // Clear the balance so the "account deletion left behind a
                // non-zero balance" check doesn't trip earlier than the desired
                // check.
                sleA1->at(sfBalance) = beast::kZero;
                BEAST_EXPECT(sleA1->at(sfOwnerCount) == 0);
                increaseOwnerCount(ac.view(), sleA1, {}, 1, ac.journal);

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"account deletion left behind a sponsorship field"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleA1 = ac.view().peek(keylet::account(a1.id()));
                if (!sleA1)
                    return false;
                sleA1->at(sfBalance) = beast::kZero;
                sleA1->setFieldU32(sfSponsoredOwnerCount, 1);

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"account deletion left behind a sponsorship field"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleA1 = ac.view().peek(keylet::account(a1.id()));
                if (!sleA1)
                    return false;
                sleA1->at(sfBalance) = beast::kZero;
                sleA1->setFieldU32(sfSponsoringOwnerCount, 1);

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"account deletion left behind a sponsorship field"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const a1Id = a1.id();
                auto const sleA1 = ac.view().peek(keylet::account(a1Id));
                if (!sleA1)
                    return false;
                sleA1->at(sfBalance) = beast::kZero;
                sleA1->setFieldU32(sfSponsoringAccountCount, 1);

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"account deletion left behind a sponsorship field"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleA1 = ac.view().peek(keylet::account(a1.id()));
                if (!sleA1)
                    return false;
                sleA1->at(sfBalance) = beast::kZero;
                sleA1->setAccountID(sfSponsor, a2.id());

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        doInvariantCheck(
            Env{*this, FeatureBitset{featureSponsor}},
            {{"account deletion left behind a sponsorship field"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleA1 = ac.view().peek(keylet::account(a1.id()));
                if (!sleA1)
                    return false;
                sleA1->at(sfBalance) = beast::kZero;
                sleA1->setAccountID(sfSponsor, a2.id());

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        for (auto const& [keyletfunc, type, includeInTests] : kDirectAccountKeylets)
        {
            if (!includeInTests)
                continue;

            using namespace std::string_literals;

            doInvariantCheck(
                {{"account deletion left behind a "s + type.cStr() + " object"}},
                // NOLINTNEXTLINE(readability-identifier-naming)
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                    // Add an object to the ledger for account A1, then delete
                    // A1
                    auto const a1 = A1.id();
                    auto sleA1 = ac.view().peek(keylet::account(a1));
                    if (!sleA1)
                        return false;

                    auto const key = std::invoke(keyletfunc, a1);
                    auto const newSLE = std::make_shared<SLE>(key);
                    ac.view().insert(newSLE);
                    // Clear the balance so the "account deletion left behind a
                    // non-zero balance" check doesn't trip earlier than the
                    // desired check.
                    sleA1->at(sfBalance) = beast::kZero;
                    ac.view().erase(sleA1);

                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});
        }

        // NFT special case
        doInvariantCheck(
            {{"account deletion left behind a NFTokenPage object"}},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                // remove an account from the view
                auto sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                // Clear the balance so the "account deletion left behind a
                // non-zero balance" check doesn't trip earlier than the desired
                // check.
                sle->at(sfBalance) = beast::kZero;
                sle->at(sfOwnerCount) = 0;
                ac.view().erase(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const&, Env& env) {
                // Preclose callback to mint the NFT which will be deleted in
                // the Precheck callback above.
                env(token::mint(a1));

                return true;
            });

        // AMM special cases
        AccountID ammAcctID;
        uint256 ammKey;
        Issue ammIssue;
        doInvariantCheck(
            {{"account deletion left behind a DirectoryNode object"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // Delete the AMM account without cleaning up the directory or
                // deleting the AMM object
                auto sle = ac.view().peek(keylet::account(ammAcctID));
                if (!sle)
                    return false;

                BEAST_EXPECT(sle->at(~sfAMMID));
                BEAST_EXPECT(sle->at(~sfAMMID) == ammKey);

                // Clear the balance so the "account deletion left behind a
                // non-zero balance" check doesn't trip earlier than the desired
                // check.
                sle->at(sfBalance) = beast::kZero;
                sle->at(sfOwnerCount) = 0;
                ac.view().erase(sle);

                return true;
            },
            XRPAmount{},
            STTx{ttAMM_WITHDRAW, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                // Preclose callback to create the AMM which will be partially
                // deleted in the Precheck callback above.
                AMM const amm(env, a1, XRP(100), a1["USD"](50));
                ammAcctID = amm.ammAccount();
                ammKey = amm.ammID();
                ammIssue = amm.lptIssue();
                return true;
            });
        doInvariantCheck(
            {{"account deletion left behind a AMM object"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // Delete all the AMM's trust lines, remove the AMM from the AMM
                // account's directory (this deletes the directory), and delete
                // the AMM account. Do not delete the AMM object.
                auto sle = ac.view().peek(keylet::account(ammAcctID));
                if (!sle)
                    return false;

                BEAST_EXPECT(sle->at(~sfAMMID));
                BEAST_EXPECT(sle->at(~sfAMMID) == ammKey);

                for (auto const& trustKeylet :
                     {keylet::trustLine(ammAcctID, a1["USD"]), keylet::trustLine(a1, ammIssue)})
                {
                    auto const line = ac.view().peek(trustKeylet);
                    if (!line)
                    {
                        return false;
                    }

                    STAmount const lowLimit = line->at(sfLowLimit);
                    STAmount const highLimit = line->at(sfHighLimit);
                    BEAST_EXPECT(
                        trustDelete(
                            ac.view(),
                            line,
                            lowLimit.getIssuer(),
                            highLimit.getIssuer(),
                            ac.journal) == tesSUCCESS);
                }

                auto const ammSle = ac.view().peek(keylet::amm(ammKey));
                if (!BEAST_EXPECT(ammSle))
                    return false;
                auto const ownerDirKeylet = keylet::ownerDir(ammAcctID);

                BEAST_EXPECT(
                    ac.view().dirRemove(ownerDirKeylet, ammSle->at(sfOwnerNode), ammKey, false));
                BEAST_EXPECT(
                    !ac.view().exists(ownerDirKeylet) || ac.view().emptyDirDelete(ownerDirKeylet));

                // Clear the balance so the "account deletion left behind a
                // non-zero balance" check doesn't trip earlier than the desired
                // check.
                sle->at(sfBalance) = beast::kZero;
                sle->at(sfOwnerCount) = 0;
                ac.view().erase(sle);

                return true;
            },
            XRPAmount{},
            STTx{ttAMM_WITHDRAW, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                // Preclose callback to create the AMM which will be partially
                // deleted in the Precheck callback above.
                AMM const amm(env, a1, XRP(100), a1["USD"](50));
                ammAcctID = amm.ammAccount();
                ammKey = amm.ammID();
                ammIssue = amm.lptIssue();
                return true;
            });
    }

    void
    testTypesMatch()
    {
        using namespace test::jtx;
        testcase << "ledger entry types don't match";
        doInvariantCheck(
            {{"ledger entry type mismatch"}, {"XRP net change of -1000000000 doesn't match fee 0"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // replace an entry in the table with an SLE of a different type
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto const sleNew = std::make_shared<SLE>(ltTICKET, sle->key());
                ac.rawView().rawReplace(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"invalid ledger entry type added"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // add an entry in the table with an SLE of an invalid type
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                // make a dummy escrow ledger entry, then change the type to an
                // unsupported value so that the valid type invariant check
                // will fail.
                auto const sleNew = std::make_shared<SLE>(
                    keylet::escrow(a1, SeqProxy::rawSequence((*sle)[sfSequence] + 2)));

                // We don't use ltNICKNAME directly since it's marked deprecated
                // to prevent accidental use elsewhere.
                sleNew->type_ = static_cast<LedgerEntryType>('n');
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testNoXRPTrustLine()
    {
        using namespace test::jtx;
        testcase << "trust lines with XRP not allowed";
        doInvariantCheck(
            {{"an XRP trust line was created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create simple trust SLE with xrp currency
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, xrpIssue().currency));
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testNoDeepFreezeTrustLinesWithoutFreeze()
    {
        using namespace test::jtx;
        testcase << "trust lines with deep freeze flag without freeze "
                    "not allowed";
        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));

                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze | lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze | lsfHighFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowFreeze | lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testTransfersNotFrozen()
    {
        using namespace test::jtx;
        testcase << "transfers when frozen";

        Account const g1{"G1"};
        // Helper function to establish the trustlines
        auto const createTrustlines = [&](Account const& a1, Account const& a2, Env& env) {
            // Preclose callback to establish trust lines with gateway
            env.fund(XRP(1000), g1);

            env.trust(g1["USD"](10000), a1);
            env.trust(g1["USD"](10000), a2);
            env.close();

            env(pay(g1, a1, g1["USD"](1000)));
            env(pay(g1, a2, g1["USD"](1000)));
            env.close();

            return true;
        };

        auto const a1FrozenByIssuer = [&](Account const& a1, Account const& a2, Env& env) {
            createTrustlines(a1, a2, env);
            env(trust(g1, a1["USD"](10000), tfSetFreeze));
            env.close();

            return true;
        };

        auto const a1DeepFrozenByIssuer = [&](Account const& a1, Account const& a2, Env& env) {
            a1FrozenByIssuer(a1, a2, env);
            env(trust(g1, a1["USD"](10000), tfSetDeepFreeze));
            env.close();

            return true;
        };

        auto const changeBalances = [&](Account const& a1,
                                        Account const& a2,
                                        ApplyContext& ac,
                                        int a1Balance,
                                        int a2Balance) {
            auto const sleA1 = ac.view().peek(keylet::trustLine(a1, g1["USD"]));
            auto const sleA2 = ac.view().peek(keylet::trustLine(a2, g1["USD"]));

            sleA1->setFieldAmount(sfBalance, g1["USD"](a1Balance));
            sleA2->setFieldAmount(sfBalance, g1["USD"](a2Balance));

            ac.view().update(sleA1);
            ac.view().update(sleA2);
        };

        // test: imitating frozen A1 making a payment to A2.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -900, -1100);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1FrozenByIssuer);

        // test: imitating deep frozen A1 making a payment to A2.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -900, -1100);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1DeepFrozenByIssuer);

        // test: imitating A2 making a payment to deep frozen A1.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -1100, -900);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1DeepFrozenByIssuer);
    }

    void
    testXRPBalanceCheck()
    {
        using namespace test::jtx;
        testcase << "XRP balance checks";

        doInvariantCheck(
            {{"Cannot return non-native STAmount as XRPAmount"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                // non-native balance
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                STAmount const nonNative(a2["USD"](51));
                sle->setFieldAmount(sfBalance, nonNative);
                ac.view().update(sle);
                return true;
            });

        doInvariantCheck(
            {{"incorrect account XRP balance"}, {"XRP net change was positive: 99999999000000001"}},
            [this](Account const& a1, Account const&, ApplyContext& ac) {
                // balance exceeds genesis amount
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                // Use `drops(1)` to bypass a call to STAmount::canonicalize
                // with an invalid value
                sle->setFieldAmount(sfBalance, kInitialXrp + drops(1));
                BEAST_EXPECT(!sle->getFieldAmount(sfBalance).negative());
                ac.view().update(sle);
                return true;
            });

        doInvariantCheck(
            {{"incorrect account XRP balance"},
             {"XRP net change of -1000000001 doesn't match fee 0"}},
            [this](Account const& a1, Account const&, ApplyContext& ac) {
                // balance is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                sle->setFieldAmount(sfBalance, STAmount{1, true});
                BEAST_EXPECT(sle->getFieldAmount(sfBalance).negative());
                ac.view().update(sle);
                return true;
            });
    }

    void
    testTransactionFeeCheck()
    {
        using namespace test::jtx;
        using namespace std::string_literals;
        testcase << "Transaction fee checks";

        doInvariantCheck(
            {{"fee paid was negative: -1"}, {"XRP net change of 0 doesn't match fee -1"}},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{-1});

        doInvariantCheck(
            {{"fee paid exceeds system limit: "s + to_string(kInitialXrp)},
             {"XRP net change of 0 doesn't match fee "s + to_string(kInitialXrp)}},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{kInitialXrp});

        doInvariantCheck(
            {{"fee paid is 20 exceeds fee specified in transaction."},
             {"XRP net change of 0 doesn't match fee 20"}},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{20},
            STTx{ttACCOUNT_SET, [](STObject& tx) { tx.setFieldAmount(sfFee, XRPAmount{10}); }});
    }

    void
    testNoBadOffers()
    {
        using namespace test::jtx;
        testcase << "no bad offers";

        doInvariantCheck(
            {{"offer with a bad amount"}}, [](Account const& a1, Account const&, ApplyContext& ac) {
                // offer with negative takerpays
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::offer(a1.id(), SeqProxy::rawSequence((*sle)[sfSequence])));
                sleNew->setAccountID(sfAccount, a1.id());
                sleNew->setFieldU32(sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount(sfTakerPays, XRP(-1));
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"offer with a bad amount"}}, [](Account const& a1, Account const&, ApplyContext& ac) {
                // offer with negative takergets
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::offer(a1.id(), SeqProxy::rawSequence((*sle)[sfSequence])));
                sleNew->setAccountID(sfAccount, a1.id());
                sleNew->setFieldU32(sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount(sfTakerPays, a1["USD"](10));
                sleNew->setFieldAmount(sfTakerGets, XRP(-1));
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"offer with a bad amount"}}, [](Account const& a1, Account const&, ApplyContext& ac) {
                // offer XRP to XRP
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::offer(a1.id(), SeqProxy::rawSequence((*sle)[sfSequence])));
                sleNew->setAccountID(sfAccount, a1.id());
                sleNew->setFieldU32(sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount(sfTakerPays, XRP(10));
                sleNew->setFieldAmount(sfTakerGets, XRP(11));
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testNoZeroEscrow()
    {
        using namespace test::jtx;
        testcase << "no zero escrow";

        doInvariantCheck(
            {{"XRP net change of -1000000 doesn't match fee 0"},
             {"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // escrow with negative amount
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(a1, SeqProxy::rawSequence((*sle)[sfSequence] + 2)));
                sleNew->setFieldAmount(sfAmount, XRP(-1));
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"XRP net change was positive: 100000000000000001"},
             {"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // escrow with too-large amount
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(a1, SeqProxy::rawSequence((*sle)[sfSequence] + 2)));
                // Use `drops(1)` to bypass a call to STAmount::canonicalize
                // with an invalid value
                sleNew->setFieldAmount(sfAmount, kInitialXrp + drops(1));
                ac.view().insert(sleNew);
                return true;
            });

        // IOU < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // escrow with too-little iou
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(a1, SeqProxy::rawSequence((*sle)[sfSequence] + 2)));

                Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
                STAmount const amt(usd, -1);
                sleNew->setFieldAmount(sfAmount, amt);
                ac.view().insert(sleNew);
                return true;
            });

        // IOU bad currency
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // escrow with bad iou currency
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(a1, SeqProxy::rawSequence((*sle)[sfSequence] + 2)));

                Issue const bad{badCurrency(), AccountID(0x4985601)};
                STAmount const amt(bad, 1);
                sleNew->setFieldAmount(sfAmount, amt);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // escrow with too-little mpt
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(a1, SeqProxy::rawSequence((*sle)[sfSequence] + 2)));

                MPTIssue const mpt{makeMptID(1, AccountID(0x4985601))};
                STAmount const amt(mpt, -1);
                sleNew->setFieldAmount(sfAmount, amt);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT OutstandingAmount < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // mptissuance outstanding is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(1, AccountID(0x4985601))};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfOutstandingAmount, -1);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT LockedAmount < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // mptissuance locked is less than locked
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(1, AccountID(0x4985601))};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfLockedAmount, -1);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT OutstandingAmount < LockedAmount
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // mptissuance outstanding is less than locked
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(1, AccountID(0x4985601))};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfOutstandingAmount, 1);
                sleNew->setFieldU64(sfLockedAmount, 10);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT MPTAmount < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // mptoken amount is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(1, AccountID(0x4985601))};
                auto sleNew = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a1));
                sleNew->setFieldU64(sfMPTAmount, -1);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT LockedAmount < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // mptoken locked amount is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(1, AccountID(0x4985601))};
                auto sleNew = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a1));
                sleNew->setFieldU64(sfLockedAmount, -1);
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testValidNewAccountRoot()
    {
        using namespace test::jtx;
        testcase << "valid new account root";

        doInvariantCheck(
            {{"account root created illegally"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                // Insert a new account root created by a non-payment into
                // the view.
                Account const a3{"A3"};
                Keylet const acctKeylet = keylet::account(a3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"multiple accounts created in a single transaction"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                // Insert two new account roots into the view.
                {
                    Account const a3{"A3"};
                    Keylet const acctKeylet = keylet::account(a3);
                    auto const sleA3 = std::make_shared<SLE>(acctKeylet);
                    ac.view().insert(sleA3);
                }
                {
                    Account const a4{"A4"};
                    Keylet const acctKeylet = keylet::account(a4);
                    auto const sleA4 = std::make_shared<SLE>(acctKeylet);
                    ac.view().insert(sleA4);
                }
                return true;
            });

        doInvariantCheck(
            {{"account created with wrong starting sequence number"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                // Insert a new account root with the wrong starting sequence.
                Account const a3{"A3"};
                Keylet const acctKeylet = keylet::account(a3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, ac.view().seq() + 1);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}});

        doInvariantCheck(
            {{"pseudo-account created by a wrong transaction type"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                Account const a3{"A3"};
                Keylet const acctKeylet = keylet::account(a3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, 0);
                sleNew->setFieldH256(sfAMMID, uint256(1));
                sleNew->setFieldU32(sfFlags, lsfDisableMaster | lsfDefaultRipple);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}});

        doInvariantCheck(
            {{"account created with wrong starting sequence number"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                Account const a3{"A3"};
                Keylet const acctKeylet = keylet::account(a3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, ac.view().seq());
                sleNew->setFieldH256(sfAMMID, uint256(1));
                sleNew->setFieldU32(sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttAMM_CREATE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"pseudo-account created with wrong flags"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                Account const a3{"A3"};
                Keylet const acctKeylet = keylet::account(a3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, 0);
                sleNew->setFieldH256(sfAMMID, uint256(1));
                sleNew->setFieldU32(sfFlags, lsfDisableMaster | lsfDefaultRipple);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"pseudo-account created with wrong flags"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                Account const a3{"A3"};
                Keylet const acctKeylet = keylet::account(a3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, 0);
                sleNew->setFieldH256(sfAMMID, uint256(1));
                sleNew->setFieldU32(
                    sfFlags,
                    lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth | lsfRequireDestTag);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttAMM_CREATE, [](STObject& tx) {}});
    }

    void
    testNFTokenPageInvariants()
    {
        using namespace test::jtx;
        testcase << "NFTokenPage";

        // lambda that returns an STArray of NFTokenIDs.
        uint256 const firstNFTID(
            "0000000000000000000000000000000000000001FFFFFFFFFFFFFFFF00000000");
        auto makeNFTokenIDs = [&firstNFTID](unsigned int nftCount) {
            SOTemplate const* nfTokenTemplate =
                InnerObjectFormats::getInstance().findSOTemplateBySField(sfNFToken);

            uint256 nftID(firstNFTID);
            STArray ret;
            for (int i = 0; i < nftCount; ++i)
            {
                STObject newNFToken(*nfTokenTemplate, sfNFToken, [&nftID](STObject& object) {
                    object.setFieldH256(sfNFTokenID, nftID);
                });
                ret.pushBack(std::move(newNFToken));
                ++nftID;
            }
            return ret;
        };

        doInvariantCheck(
            {{"NFT page has invalid size"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftokenPageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(0));

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page has invalid size"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftokenPageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(33));

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFTs on page are not sorted"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(2);
                std::iter_swap(nfTokens.begin(), nfTokens.begin() + 1);

                auto nftPage = std::make_shared<SLE>(keylet::nftokenPageMax(a1));
                nftPage->setFieldArray(sfNFTokens, nfTokens);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT contains empty URI"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(1);
                nfTokens[0].setFieldVL(sfURI, Blob{});

                auto nftPage = std::make_shared<SLE>(keylet::nftokenPageMax(a1));
                nftPage->setFieldArray(sfNFTokens, nfTokens);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftokenPageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(sfPreviousPageMin, keylet::nftokenPageMax(a1).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftokenPageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(sfPreviousPageMin, keylet::nftokenPageMin(a2).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftokenPageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(sfNextPageMin, nftPage->key());

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](Account const& a1, Account const& a2, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(1);
                auto nftPage = std::make_shared<SLE>(keylet::nftokenPage(
                    keylet::nftokenPageMax(a1), ++(nfTokens[0].getFieldH256(sfNFTokenID))));
                nftPage->setFieldArray(sfNFTokens, nfTokens);
                nftPage->setFieldH256(sfNextPageMin, keylet::nftokenPageMax(a2).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT found in incorrect page"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(2);
                auto nftPage = std::make_shared<SLE>(keylet::nftokenPage(
                    keylet::nftokenPageMax(a1), (nfTokens[1].getFieldH256(sfNFTokenID))));
                nftPage->setFieldArray(sfNFTokens, nfTokens);

                ac.view().insert(nftPage);
                return true;
            });
    }

    void
    testAMMDeleteInvariants(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const enforceAMMDelete = features[fixCleanup3_3_0];
        testcase << "AMM delete invariants" + std::string(enforceAMMDelete ? " fix" : "");

        Env env(*this, features);
        Account const issuer{"issuer"};
        Issue const lptIssue{Currency(0x4c50540000000000), issuer.id()};
        STAmount const zeroLP{lptIssue, 0};
        STAmount const nonZeroLP{lptIssue, 1};

        auto const makeAMM = [](STAmount const& lptBalance) {
            auto sleAMM = std::make_shared<SLE>(keylet::amm(uint256(1)));
            sleAMM->setFieldAmount(sfLPTokenBalance, lptBalance);
            return sleAMM;
        };

        auto const checkInvariant = [&](TxType txType,
                                        TER result,
                                        std::optional<STAmount> const& deletedLPBalance,
                                        bool expected,
                                        std::string const& expectedLog) {
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ValidAMM invariant;

            if (deletedLPBalance)
                invariant.visitEntry(true, makeAMM(*deletedLPBalance), nullptr);

            bool const actual = invariant.finalize(
                STTx{txType, [](STObject&) {}}, result, XRPAmount{}, *env.current(), jlog);

            BEAST_EXPECTS(actual == expected, "unexpected AMM delete invariant result");
            auto const messages = sink.messages().str();
            auto const expectedLogWhenEnforced = enforceAMMDelete ? expectedLog : "";
            if (!expectedLogWhenEnforced.empty())
            {
                BEAST_EXPECTS(messages.contains(expectedLogWhenEnforced), expectedLogWhenEnforced);
            }
            else
            {
                BEAST_EXPECTS(messages.empty(), messages);
            }
        };

        checkInvariant(
            ttPAYMENT,
            tesSUCCESS,
            nonZeroLP,
            !enforceAMMDelete,
            "Invariant failed: AMM failed, unexpected AMM deletion by");
        checkInvariant(
            ttAMM_DELETE,
            tesSUCCESS,
            std::nullopt,
            !enforceAMMDelete,
            "Invariant failed: AMMDelete failed, AMM object remained on tesSUCCESS");
        checkInvariant(
            ttAMM_DELETE,
            tesSUCCESS,
            nonZeroLP,
            !enforceAMMDelete,
            "Invariant failed: AMMDelete failed, AMM object deleted with non-zero LP balance");
        checkInvariant(
            ttAMM_DELETE,
            tecINCOMPLETE,
            zeroLP,
            !enforceAMMDelete,
            "Invariant failed: AMMDelete failed, AMM object deleted when result is not tesSUCCESS");

        checkInvariant(ttAMM_WITHDRAW, tesSUCCESS, nonZeroLP, true, "");
        checkInvariant(ttAMM_CLAWBACK, tesSUCCESS, nonZeroLP, true, "");

        checkInvariant(ttAMM_DELETE, tesSUCCESS, zeroLP, true, "");
        checkInvariant(ttAMM_WITHDRAW, tesSUCCESS, zeroLP, true, "");
        checkInvariant(ttAMM_CLAWBACK, tesSUCCESS, zeroLP, true, "");
    }

    static SLE::pointer
    createPermissionedDomain(
        ApplyContext& ac,
        test::jtx::Account const& a1,
        test::jtx::Account const& a2,
        std::uint32_t numCreds = 2,
        std::uint32_t seq = 10)
    {
        Keylet const pdKeylet = keylet::permissionedDomain(a1.id(), SeqProxy::rawSequence(seq));
        auto sle = std::make_shared<SLE>(pdKeylet);

        sle->setAccountID(sfOwner, a1);
        sle->setFieldU32(sfSequence, seq);

        if (numCreds != 0u)
        {
            // This array is sorted naturally, but if you are going to change
            // this behavior, don't forget to use credentials::makeSorted
            STArray credentials(sfAcceptedCredentials, numCreds);
            for (std::size_t n = 0; n < numCreds; ++n)
            {
                auto cred = STObject::makeInnerObject(sfCredential);
                cred.setAccountID(sfIssuer, a2);
                auto credType = "cred_type" + std::to_string(n);
                cred.setFieldVL(sfCredentialType, Slice(credType.c_str(), credType.size()));
                credentials.pushBack(std::move(cred));
            }
            sle->setFieldArray(sfAcceptedCredentials, credentials);
        }

        ac.view().insert(sle);
        return sle;
    };

    void
    testPermissionedDomainInvariants(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const fixEnabled = features[fixCleanup3_1_3];
        std::initializer_list<TER> const badTers = {tecINVARIANT_FAILED, tecINVARIANT_FAILED};
        std::initializer_list<TER> const failTers = {tecINVARIANT_FAILED, tefINVARIANT_FAILED};

        testcase << "PermissionedDomain" + std::string(fixEnabled ? " fix" : "");

        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain with no rules."}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                return createPermissionedDomain(ac, a1, a2, 0).get();
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain 2";

        static constexpr auto kTooBig = kMaxPermissionedDomainCredentialsArraySize + 1;
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain bad credentials size " + std::to_string(kTooBig)}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                return !!createPermissionedDomain(ac, a1, a2, kTooBig);
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain 3";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain credentials aren't sorted"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto slePd = createPermissionedDomain(ac, a1, a2, 0);

                STArray credentials(sfAcceptedCredentials, 2);
                for (std::size_t n = 0; n < 2; ++n)
                {
                    auto cred = STObject::makeInnerObject(sfCredential);
                    cred.setAccountID(sfIssuer, a2);
                    auto credType = std::string("cred_type") + std::to_string(9 - n);
                    cred.setFieldVL(sfCredentialType, Slice(credType.c_str(), credType.size()));
                    credentials.pushBack(std::move(cred));
                }
                slePd->setFieldArray(sfAcceptedCredentials, credentials);
                ac.view().update(slePd);
                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain 4";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain credentials aren't unique"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto slePd = createPermissionedDomain(ac, a1, a2, 0);

                STArray credentials(sfAcceptedCredentials, 2);
                for (std::size_t n = 0; n < 2; ++n)
                {
                    auto cred = STObject::makeInnerObject(sfCredential);
                    cred.setAccountID(sfIssuer, a2);
                    cred.setFieldVL(sfCredentialType, Slice("cred_type", 9));
                    credentials.pushBack(std::move(cred));
                }
                slePd->setFieldArray(sfAcceptedCredentials, credentials);
                ac.view().update(slePd);
                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain Set 1";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain with no rules."}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create PD
                auto slePd = createPermissionedDomain(ac, a1, a2);

                // update PD with empty rules
                {
                    STArray const credentials(sfAcceptedCredentials, 2);
                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain Set 2";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain bad credentials size " + std::to_string(kTooBig)}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create PD
                auto slePd = createPermissionedDomain(ac, a1, a2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, kTooBig);

                    for (std::size_t n = 0; n < kTooBig; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, a2);
                        auto credType = "cred_type2" + std::to_string(n);
                        cred.setFieldVL(sfCredentialType, Slice(credType.c_str(), credType.size()));
                        credentials.pushBack(std::move(cred));
                    }

                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain Set 3";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain credentials aren't sorted"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create PD
                auto slePd = createPermissionedDomain(ac, a1, a2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, 2);
                    for (std::size_t n = 0; n < 2; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, a2);
                        auto credType = std::string("cred_type2") + std::to_string(9 - n);
                        cred.setFieldVL(sfCredentialType, Slice(credType.c_str(), credType.size()));
                        credentials.pushBack(std::move(cred));
                    }

                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain Set 4";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain credentials aren't unique"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create PD
                auto slePd = createPermissionedDomain(ac, a1, a2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, 2);
                    for (std::size_t n = 0; n < 2; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, a2);
                        cred.setFieldVL(sfCredentialType, Slice("cred_type", 9));
                        credentials.pushBack(std::move(cred));
                    }
                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        std::initializer_list<TER> const goodTers = {tesSUCCESS, tesSUCCESS};

        std::vector<std::string> const badMoreThan1{
            {"transaction affected more than 1 permissioned domain entry."}};
        std::vector<std::string> const emptyV;
        std::vector<std::string> const badNoDomains{{"no domain objects affected by"}};
        std::vector<std::string> const badNotDeleted{
            {"domain object modified, but not deleted by "}};
        std::vector<std::string> const badDeleted{{"domain object deleted by"}};
        std::vector<std::string> const badTx{
            {"domain object(s) affected by an unauthorized transaction."}};

        {
            testcase << "PermissionedDomain set 2 domains ";
            doInvariantCheck(
                makeEnv(features),
                fixEnabled ? badMoreThan1 : emptyV,
                [](Account const& a1, Account const& a2, ApplyContext& ac) {
                    createPermissionedDomain(ac, a1, a2);
                    createPermissionedDomain(ac, a1, a2, 2, 11);
                    return true;
                },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
                fixEnabled ? failTers : goodTers);
        }

        {
            testcase << "PermissionedDomain del 2 domains";

            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            [[maybe_unused]] auto [seq2, pd2] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                fixEnabled ? badMoreThan1 : emptyV,
                [&pd1, &pd2](Account const&, Account const&, ApplyContext& ac) {
                    auto sle1 = ac.view().peek({ltPERMISSIONED_DOMAIN, pd1});
                    auto sle2 = ac.view().peek({ltPERMISSIONED_DOMAIN, pd2});
                    ac.view().erase(sle1);
                    ac.view().erase(sle2);
                    return true;
                },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_DELETE, [](STObject&) {}},
                fixEnabled ? failTers : goodTers);
        }

        {
            testcase << "PermissionedDomain set 0 domains ";
            doInvariantCheck(
                makeEnv(features),
                fixEnabled ? badNoDomains : emptyV,
                [](Account const&, Account const&, ApplyContext&) { return true; },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
                fixEnabled ? badTers : goodTers);
        }

        {
            testcase << "PermissionedDomain del 0 domains";

            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            [[maybe_unused]] auto [seq2, pd2] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                makeEnv(features),
                a1,
                a2,
                fixEnabled ? badNoDomains : emptyV,
                [](Account const&, Account const&, ApplyContext&) { return true; },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_DELETE, [](STObject&) {}},
                fixEnabled ? badTers : goodTers);
        }

        {
            testcase << "PermissionedDomain set, delete domain";

            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                fixEnabled ? badDeleted : emptyV,
                [&pd1](Account const&, Account const&, ApplyContext& ac) {
                    auto sle1 = ac.view().peek({ltPERMISSIONED_DOMAIN, pd1});
                    ac.view().erase(sle1);
                    return true;
                },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
                fixEnabled ? failTers : goodTers);
        }

        {
            testcase << "PermissionedDomain del, create domain ";
            doInvariantCheck(
                makeEnv(features),
                fixEnabled ? badNotDeleted : emptyV,
                [](Account const& a1, Account const& a2, ApplyContext& ac) {
                    createPermissionedDomain(ac, a1, a2);
                    return true;
                },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_DELETE, [](STObject&) {}},
                fixEnabled ? failTers : goodTers);
        }

        {
            testcase << "PermissionedDomain invalid tx";

            doInvariantCheck(
                fixEnabled ? badTx : emptyV,
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    createPermissionedDomain(ac, a1, a2);
                    return true;
                },
                XRPAmount{},
                STTx{ttPAYMENT, [](STObject&) {}},
                failTers);
        }
    }

    void
    testValidPseudoAccounts()
    {
        testcase << "valid pseudo accounts";

        using namespace jtx;

        AccountID pseudoAccountID;
        Preclose const createPseudo = [&, this](Account const& a, Account const& b, Env& env) {
            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

            // Create vault
            Vault const vault{env};
            auto [tx, vKeylet] = vault.create({.owner = a, .asset = xrpAsset});
            env(tx);
            env.close();
            if (auto const vSle = env.le(vKeylet); BEAST_EXPECT(vSle))
            {
                pseudoAccountID = vSle->at(sfAccount);
            }

            return BEAST_EXPECT(env.le(keylet::account(pseudoAccountID)));
        };

        /* Cases to check
            "pseudo-account has 0 pseudo-account fields set"
            "pseudo-account has 2 pseudo-account fields set"
            "pseudo-account sequence changed"
            "pseudo-account flags are not set"
            "pseudo-account has a regular key"
            "pseudo-account has a sponsorship field"
        */
        struct Mod
        {
            std::string expectedFailure;
            std::function<void(SLE::pointer&)> func;
        };
        auto const mods = std::to_array<Mod>({
            {
                .expectedFailure = "pseudo-account has 0 pseudo-account fields set",
                .func =
                    [this](SLE::pointer& sle) {
                        BEAST_EXPECT(sle->at(~sfVaultID));
                        sle->at(~sfVaultID) = std::nullopt;
                    },
            },
            {
                .expectedFailure = "pseudo-account sequence changed",
                .func = [](SLE::pointer& sle) { sle->at(sfSequence) = 12345; },
            },
            {
                .expectedFailure = "pseudo-account flags are not set",
                .func = [](SLE::pointer& sle) { sle->at(sfFlags) = lsfNoFreeze; },
            },
            {
                .expectedFailure = "pseudo-account has a regular key",
                .func = [](SLE::pointer& sle) { sle->at(sfRegularKey) = Account("regular").id(); },
            },
            {
                .expectedFailure = "pseudo-account has a sponsorship field",
                .func = [](SLE::pointer& sle) { sle->at(sfSponsoredOwnerCount) = 1; },
            },
            {
                .expectedFailure = "pseudo-account has a sponsorship field",
                .func = [](SLE::pointer& sle) { sle->at(sfSponsoringOwnerCount) = 1; },
            },
            {
                .expectedFailure = "pseudo-account has a sponsorship field",
                .func = [](SLE::pointer& sle) { sle->at(sfSponsoringAccountCount) = 1; },
            },
            {
                .expectedFailure = "pseudo-account has a sponsorship field",
                .func = [](SLE::pointer& sle) { sle->at(sfSponsor) = Account("sponsor").id(); },
            },
        });

        for (auto const& mod : mods)
        {
            doInvariantCheck(
                {{mod.expectedFailure}},
                [&](Account const& a1, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::account(pseudoAccountID));
                    if (!sle)
                        return false;
                    mod.func(sle);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createPseudo);
        }
        for (auto const pField : getPseudoAccountFields())
        {
            // createPseudo creates a vault, so sfVaultID will be set, and
            // setting it again will not cause an error
            if (pField == &sfVaultID)
                continue;
            doInvariantCheck(
                {{"pseudo-account has 2 pseudo-account fields set"}},
                [&](Account const& a1, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::account(pseudoAccountID));
                    if (!sle)
                        return false;

                    auto const vaultID = ~sle->at(~sfVaultID);
                    BEAST_EXPECT(vaultID && !sle->isFieldPresent(*pField));
                    sle->setFieldH256(*pField, *vaultID);

                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createPseudo);
        }

        // Take one of the regular accounts and set the sequence to 0, which
        // will make it look like a pseudo-account
        doInvariantCheck(
            {{"pseudo-account has 0 pseudo-account fields set"},
             {"pseudo-account sequence changed"},
             {"pseudo-account flags are not set"}},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                sle->at(sfSequence) = 0;
                ac.view().update(sle);
                return true;
            });
    }

    void
    testPseudoAccountLoanBrokerLink()
    {
        testcase << "pseudo-account loan-broker link";
        using namespace jtx;

        // The finalize-time walk in ValidPseudoAccounts verifies that every
        // touched pseudo-account's sfLoanBrokerID resolves to a live broker
        // whose sfAccount points back at the pseudo. Repoint the pseudo-account
        // at a keylet with no broker so the lookup returns nothing.
        Keylet loanBrokerKeylet = keylet::amendments();
        Preclose const createBroker = [&, this](Account const& a, Account const&, Env& env) {
            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            loanBrokerKeylet = this->createLoanBroker(a, env, xrpAsset);
            return BEAST_EXPECT(env.le(loanBrokerKeylet));
        };

        doInvariantCheck(
            {{"pseudo-account LoanBrokerID does not resolve to a broker "
              "referencing this account"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sleBroker = ac.view().peek(loanBrokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return false;
                auto slePseudo = ac.view().peek(keylet::account(sleBroker->at(sfAccount)));
                if (!BEAST_EXPECT(slePseudo))
                    return false;
                // Point the pseudo-account at a keylet with no broker.
                slePseudo->at(sfLoanBrokerID) = uint256(42);
                ac.view().update(slePseudo);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createBroker);
    }

    static std::pair<std::uint32_t, uint256>
    createPermissionedDomainEnv(
        test::jtx::Env& env,
        test::jtx::Account const& a1,
        test::jtx::Account const& a2,
        std::uint32_t numCreds = 2)
    {
        using namespace test::jtx;

        pdomain::Credentials credentials;

        for (std::size_t n = 0; n < numCreds; ++n)
        {
            auto credType = "cred_type" + std::to_string(n);
            credentials.push_back({.issuer = a2, .credType = credType});
        }

        std::uint32_t const seq = env.seq(a1);
        env(pdomain::setTx(a1, credentials));
        uint256 const key = pdomain::getNewDomain(env.meta());

        // std::cout << "PD, acc: " << A1.id() << ", seq: " << seq << ", k: " <<
        // key << std::endl;
        return {seq, key};
    }

    void
    testPermissionedDEX(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const fixEnabled = features[fixCleanup3_1_3];

        testcase << "PermissionedDEX" + std::string(fixEnabled ? " fix" : "");

        doInvariantCheck(
            makeEnv(features),
            {{"domain doesn't exist"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                Keylet const offerKey = keylet::offer(a1.id(), SeqProxy::rawSequence(10));
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, a1);
                sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{
                ttOFFER_CREATE,
                [](STObject& tx) {
                    tx.setFieldH256(
                        sfDomainID,
                        uint256{"F10D0CC9A0F9A3CBF585B80BE09A186483668FDBDD39AA7E33"
                                "70F3649CE134E5"});
                    Account const a1{"A1"};
                    tx.setFieldAmount(sfTakerPays, a1["USD"](10));
                    tx.setFieldAmount(sfTakerGets, XRP(1));
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // missing domain ID in offer object
        doInvariantCheck(
            makeEnv(features),
            {{"hybrid offer is malformed"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, a2);
                sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                sleOffer->setFlag(lsfHybrid);

                STArray bookArr;
                bookArr.pushBack(STObject::makeInnerObject(sfBook));
                sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{ttOFFER_CREATE, [&](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // more than one entry in sfAdditionalBooks
        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                {{"hybrid offer is malformed"}},
                [&pd1](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    sleOffer->setFlag(lsfHybrid);
                    sleOffer->setFieldH256(sfDomainID, pd1);

                    STArray bookArr;
                    bookArr.pushBack(STObject::makeInnerObject(sfBook));
                    bookArr.pushBack(STObject::makeInnerObject(sfBook));
                    sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{ttOFFER_CREATE, [&](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        // empty sfAdditionalBooks (size 0)
        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                fixEnabled ? std::vector<std::string>{{"hybrid offer is malformed"}}
                           : std::vector<std::string>{},
                [&pd1](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    sleOffer->setFlag(lsfHybrid);
                    sleOffer->setFieldH256(sfDomainID, pd1);

                    STArray const bookArr;  // empty array, size 0
                    sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{ttOFFER_CREATE, [&](STObject&) {}},
                fixEnabled ? std::initializer_list<TER>{tecINVARIANT_FAILED, tecINVARIANT_FAILED}
                           : std::initializer_list<TER>{tesSUCCESS, tesSUCCESS});
        }

        // hybrid offer missing sfAdditionalBooks
        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                {{"hybrid offer is malformed"}},
                [&pd1](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    sleOffer->setFlag(lsfHybrid);
                    sleOffer->setFieldH256(sfDomainID, pd1);
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{ttOFFER_CREATE, [&](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            [[maybe_unused]] auto [seq2, pd2] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                {{"transaction consumed wrong domains"}},
                [&pd1](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    sleOffer->setFieldH256(sfDomainID, pd1);
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttOFFER_CREATE,
                    [&pd2, &a1](STObject& tx) {
                        tx.setFieldH256(sfDomainID, pd2);
                        tx.setFieldAmount(sfTakerPays, a1["USD"](10));
                        tx.setFieldAmount(sfTakerGets, XRP(1));
                    }},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                {{"domain transaction affected regular offers"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttOFFER_CREATE,
                    [&](STObject& tx) {
                        Account const a1{"A1"};
                        tx.setFieldH256(sfDomainID, pd1);
                        tx.setFieldAmount(sfTakerPays, a1["USD"](10));
                        tx.setFieldAmount(sfTakerGets, XRP(1));
                    }},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }
    }

    void
    testPermissionedDEXDeletedOfferFallback()
    {
        using namespace test::jtx;

        testcase << "PermissionedDEX null after";

        // Tx is OfferCreate on pd2. Tracking pd1 fails the invariant iff that
        // domain lands in the set finalize consults. after == null is never
        // tracked (pre-340: after-only; post-340: early return) — same result,
        // both sides are coverage/regression that we do not fall back to before.
        auto const check = [this](
                               FeatureBitset features,
                               bool const afterIsNull,
                               bool const isDelete,
                               bool const expectInvariantFailure) {
            Env env(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env, a1, a2);
            [[maybe_unused]] auto [seq2, pd2] = createPermissionedDomainEnv(env, a1, a2);
            env.close();

            auto sleOffer =
                std::make_shared<SLE>(keylet::offer(a2.id(), SeqProxy::rawSequence(10)));
            sleOffer->setAccountID(sfAccount, a2);
            sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
            sleOffer->setFieldAmount(sfTakerGets, XRP(1));
            sleOffer->setFieldH256(sfDomainID, pd1);

            CurrentTransactionRulesGuard const rulesGuard(env.current()->rules());

            ValidPermissionedDEX invariant;
            if (afterIsNull)
            {
                // Defensive path: after is null. Must not fall back to before.
                invariant.visitEntry(isDelete, sleOffer, nullptr);
            }
            else
            {
                // Normal / real-erase path: after is the offer on pd1.
                invariant.visitEntry(isDelete, nullptr, sleOffer);
            }

            STTx const tx{ttOFFER_CREATE, [&pd2, &a1](STObject& tx) {
                              tx.setFieldH256(sfDomainID, pd2);
                              tx.setFieldAmount(sfTakerPays, a1["USD"](10));
                              tx.setFieldAmount(sfTakerGets, XRP(1));
                          }};

            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            bool const passed =
                invariant.finalize(tx, tesSUCCESS, XRPAmount{}, *env.current(), jlog);
            BEAST_EXPECT(passed != expectInvariantFailure);
            if (expectInvariantFailure)
            {
                BEAST_EXPECT(sink.messages().str().contains("transaction consumed wrong domains"));
            }
            else
            {
                BEAST_EXPECT(sink.messages().str().empty());
            }
        };

        auto const pre = defaultAmendments() - fixCleanup3_4_0;
        auto const post = defaultAmendments() | fixCleanup3_4_0;

        // after == null: not tracked
        check(pre, true, true, false);
        check(post, true, true, false);

        // after == offer on pd1
        // pre-340: domainsOld_ (delete still inserted) → fail
        check(pre, false, true, true);
        // post-340: isDelete → only domainsOld_ → pass; !isDelete → domains_ → fail
        check(post, false, true, false);
        check(post, false, false, true);
    }

    void
    testBookDirectoryExchangeRate()
    {
        using namespace test::jtx;
        testcase << "book directory exchange rate";

        auto const getBookRootKey = [](Account const& account, std::uint64_t quality) {
            Book const book{xrpIssue(), account["USD"], std::nullopt};
            return keylet::quality(keylet::book(book), quality);
        };

        // Root book-directory pages carry exchange-rate metadata that must
        // match the quality encoded in the directory key.
        auto const makeRootPage = [](Keylet const& dir, std::uint64_t exchangeRate) {
            auto sleDir = std::make_shared<SLE>(dir);
            sleDir->setFieldH256(sfRootIndex, dir.key);
            STVector256 indexes;
            indexes.pushBack(uint256{1});
            sleDir->setFieldV256(sfIndexes, indexes);
            sleDir->setFieldU64(sfExchangeRate, exchangeRate);
            return sleDir;
        };

        // Child pages do not carry quality metadata; they only point back to
        // the root directory.
        auto const makeChildPage = [](Keylet const& rootDir) {
            auto sleDir = std::make_shared<SLE>(keylet::page(rootDir, 1));
            sleDir->setFieldH256(sfRootIndex, rootDir.key);
            STVector256 indexes;
            indexes.pushBack(uint256{2});
            sleDir->setFieldV256(sfIndexes, indexes);
            return sleDir;
        };

        auto const makeOfferCreateTx = [] {
            return STTx{ttOFFER_CREATE, [](STObject& tx) {
                            Account const account{"A1"};
                            tx.setFieldAmount(sfTakerPays, XRP(1));
                            tx.setFieldAmount(sfTakerGets, account["USD"](1));
                        }};
        };
        std::initializer_list<TER> const failTers = {tecINVARIANT_FAILED, tefINVARIANT_FAILED};

        // Creating a root book directory with mismatched exchange-rate
        // metadata violates the invariant.
        doInvariantCheck(
            {{"book directory exchange rate does not match directory quality"}},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const directoryQuality = STAmount::kURateOne;
                auto const dir = getBookRootKey(a1, directoryQuality);
                ac.view().insert(makeRootPage(dir, directoryQuality + 1));
                return true;
            },
            XRPAmount{},
            makeOfferCreateTx(),
            failTers);

        // A new child page must point to an existing root page.
        doInvariantCheck(
            {{"book directory root missing"}},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const directoryQuality = STAmount::kURateOne;
                auto const rootDir = getBookRootKey(a1, directoryQuality);
                // Insert only the child page.  It points at rootDir, but the
                // corresponding root page is intentionally missing.
                ac.view().insert(makeChildPage(rootDir));
                return true;
            },
            XRPAmount{},
            makeOfferCreateTx(),
            failTers);

        // Legacy bad-root tolerance:
        // - The view contains a pre-existing root page with bad sfExchangeRate
        //   metadata.
        // - The simulated transaction only creates a child page pointing to
        //   that root.
        // - The invariant must pass because this transaction did not create
        //   the bad root, only adding a child page.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            env.fund(XRP(1000), a1);
            env.close();

            OpenView view{*env.current()};
            auto const directoryQuality = STAmount::kURateOne;
            auto const rootDir = getBookRootKey(a1, directoryQuality);
            view.rawInsert(makeRootPage(rootDir, directoryQuality + 1));

            ValidBookDirectory invariant;
            invariant.visitEntry(false, nullptr, makeChildPage(rootDir));

            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            BEAST_EXPECT(
                invariant.finalize(makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, jlog));
        }

        // A bad root is rejected when added, ignored when a legacy bad root is
        // modified without changing sfRootIndex or deleted, and checked when a
        // modified directory changes sfRootIndex.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            env.fund(XRP(1000), a1);
            env.close();

            OpenView view{*env.current()};
            auto const directoryQuality = STAmount::kURateOne;
            auto const rootDir = getBookRootKey(a1, directoryQuality);
            auto const missingRootDir = getBookRootKey(a1, directoryQuality + 1);
            auto const badRoot = makeRootPage(rootDir, directoryQuality + 1);
            view.rawInsert(badRoot);

            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};

            {
                // add
                ValidBookDirectory invariant;
                invariant.visitEntry(false, nullptr, badRoot);

                BEAST_EXPECT(
                    !invariant.finalize(makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, jlog));
            }
            {
                // modify (without changing the sfRootIndex)
                ValidBookDirectory invariant;
                invariant.visitEntry(false, badRoot, badRoot);

                BEAST_EXPECT(
                    invariant.finalize(makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, jlog));
            }
            {
                // modify (changing sfRootIndex to a missing root)
                auto const childBefore = makeChildPage(rootDir);
                auto const childAfter = std::make_shared<SLE>(*childBefore, childBefore->key());
                childAfter->setFieldH256(sfRootIndex, missingRootDir.key);

                ValidBookDirectory invariant;
                invariant.visitEntry(false, childBefore, childAfter);

                test::StreamSink missingRootSink{beast::Severity::Warning};
                beast::Journal const missingRootJlog{missingRootSink};
                BEAST_EXPECT(!invariant.finalize(
                    makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, missingRootJlog));
                BEAST_EXPECT(
                    missingRootSink.messages().str().contains("book directory root missing"));
            }
            {
                // delete
                view.rawErase(badRoot);
                BEAST_EXPECT(!view.exists(rootDir));

                ValidBookDirectory invariant;
                invariant.visitEntry(true, badRoot, badRoot);
                BEAST_EXPECT(
                    invariant.finalize(makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, jlog));
            }
        }
    }

    Keylet
    createLoanBroker(jtx::Account const& a, jtx::Env& env, jtx::PrettyAsset const& asset)
    {
        using namespace jtx;

        // Create vault
        uint256 vaultID;
        Vault const vault{env};
        auto [tx, vKeylet] = vault.create({.owner = a, .asset = asset});
        env(tx);
        BEAST_EXPECT(env.le(vKeylet));

        vaultID = vKeylet.key;

        // Create Loan Broker
        using namespace loan_broker;

        auto const loanBrokerKeylet = keylet::loanBroker(a.id(), SeqProxy::rawSequence(env.seq(a)));
        // Create a Loan Broker with all default values.
        env(set(a, vaultID), Fee(kIncrement));

        return loanBrokerKeylet;
    };

    void
    testNoModifiedUnmodifiableFields()
    {
        testcase("no modified unmodifiable fields");
        using namespace jtx;

        // Initialize with a placeholder value because there's no default ctor
        Keylet loanBrokerKeylet = keylet::amendments();
        Preclose const createLoanBroker = [&, this](Account const& a, Account const& b, Env& env) {
            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

            loanBrokerKeylet = this->createLoanBroker(a, env, xrpAsset);
            return BEAST_EXPECT(env.le(loanBrokerKeylet));
        };

        {
            auto const mods = std::to_array<std::function<void(SLE::pointer&)>>({
                [](SLE::pointer& sle) { sle->at(sfSequence) += 1; },
                [](SLE::pointer& sle) { sle->at(sfOwnerNode) += 1; },
                [](SLE::pointer& sle) { sle->at(sfVaultNode) += 1; },
                [](SLE::pointer& sle) { sle->at(sfVaultID) = uint256(1u); },
                [](SLE::pointer& sle) { sle->at(sfAccount) = sle->at(sfOwner); },
                [](SLE::pointer& sle) { sle->at(sfOwner) = sle->at(sfAccount); },
                [](SLE::pointer& sle) { sle->at(sfManagementFeeRate) += 1; },
                [](SLE::pointer& sle) { sle->at(sfCoverRateMinimum) += 1; },
                [](SLE::pointer& sle) { sle->at(sfCoverRateLiquidation) += 1; },
                [](SLE::pointer& sle) { sle->at(sfLedgerEntryType) += 1; },
                [](SLE::pointer& sle) { sle->at(sfLedgerIndex) = sle->at(sfVaultID).value(); },
            });

            for (auto const& mod : mods)
            {
                doInvariantCheck(
                    {{"changed an unchangeable field"}},
                    [&](Account const& a1, Account const&, ApplyContext& ac) {
                        auto sle = ac.view().peek(loanBrokerKeylet);
                        if (!sle)
                            return false;
                        mod(sle);
                        ac.view().update(sle);
                        return true;
                    },
                    XRPAmount{},
                    STTx{ttACCOUNT_SET, [](STObject& tx) {}},
                    {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                    createLoanBroker);
            }
        }

        // Loan flag immutability lives in NoModifiedUnmodifiableFields's
        // ltLOAN case: lsfLoanOverpayment must never toggle in either
        // direction, and lsfLoanDefault (gated on featureLendingProtocolV1_1)
        // may only transition from unset to set. Each case needs a loan that
        // already exists in the base ledger so the apply-view modification is
        // seen as a before/after change; the shared harness cannot seed one,
        // hence the bespoke view construction below.
        {
            struct Case
            {
                std::uint32_t before;
                std::uint32_t after;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                {.before = lsfLoanOverpayment,
                 .after = 0,
                 .expected = "lsfLoanOverpayment flag toggled on immutable ledger entry"},
                {.before = 0,
                 .after = lsfLoanOverpayment,
                 .expected = "lsfLoanOverpayment flag toggled on immutable ledger entry"},
                {.before = lsfLoanDefault,
                 .after = 0,
                 .expected = "lsfLoanDefault flag cleared on immutable ledger entry"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, defaultAmendments()};
                Account const a1{"A1"};
                env.fund(XRP(1000), a1);
                env.close();

                OpenView ov{*env.current()};

                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, c.before);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{ttACCOUNT_SET, [](STObject&) {}};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, c.after);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(c.expected));
            }
        }

        // Pre-featureLendingProtocolV1_1 sibling of the lsfLoanOverpayment
        // cases above: the same set-once immutability was originally enforced
        // by ValidLoan::finalize, so with V1_1 disabled toggling the flag
        // must trip that legacy check instead. lsfLoanDefault immutability
        // did not exist pre-V1_1 and is not tested here.
        {
            auto const cases = std::to_array<std::pair<std::uint32_t, std::uint32_t>>({
                {lsfLoanOverpayment, 0},
                {0, lsfLoanOverpayment},
            });

            for (auto const& [before, after] : cases)
            {
                Env env{*this, defaultAmendments() - featureLendingProtocolV1_1};
                Account const a1{"A1"};
                env.fund(XRP(1000), a1);
                env.close();

                OpenView ov{*env.current()};

                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, before);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{ttACCOUNT_SET, [](STObject&) {}};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, after);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains("Loan Overpayment flag changed"));
            }
        }

        // VaultKind, SubscriptionDate and RedemptionDate are immutable once set at creation.
        // Enforced by NoModifiedUnmodifiableFields on ltVAULT via kFieldChanged.
        Keylet closedEndedVaultKeylet = keylet::amendments();
        Preclose const createClosedEndedVault = [&, this](
                                                    Account const& a, Account const&, Env& env) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto const red = sub + kMinInvestmentPeriod + 1'000'000;
            Vault const vault{env};
            auto [tx, keylet] = vault.create(
                {.owner = a,
                 .asset = xrpIssue(),
                 .vaultKind = std::to_underlying(VaultKind::ClosedEnded),
                 .subscriptionDate = sub,
                 .redemptionDate = red});
            env(tx);
            closedEndedVaultKeylet = keylet;
            return BEAST_EXPECT(env.le(closedEndedVaultKeylet));
        };

        {
            // Each mutation must keep the vault otherwise valid so that only the immutability check
            // fires. Shifting both dates by the same offset preserves the gap; bumping sfVaultKind
            // stays within the recognised range.
            auto const mods = std::to_array<std::function<void(SLE::pointer&)>>({
                [](SLE::pointer& sle) { sle->at(sfVaultKind) += 1; },
                [](SLE::pointer& sle) { sle->at(sfSubscriptionDate) += 1; },
                [](SLE::pointer& sle) { sle->at(sfRedemptionDate) += 1; },
            });

            for (auto const& mod : mods)
            {
                doInvariantCheck(
                    {{"changed an unchangeable field"}},
                    [&](Account const&, Account const&, ApplyContext& ac) {
                        auto sle = ac.view().peek(closedEndedVaultKeylet);
                        if (!sle)
                            return false;
                        mod(sle);
                        ac.view().update(sle);
                        return true;
                    },
                    XRPAmount{},
                    STTx{ttACCOUNT_SET, [](STObject&) {}},
                    {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                    createClosedEndedVault);
            }
        }

        {
            auto const mods = std::to_array<std::function<void(SLE::pointer&)>>({
                [](SLE::pointer& sle) { sle->at(sfLedgerEntryType) += 1; },
                [](SLE::pointer& sle) { sle->at(sfLedgerIndex) = uint256(1u); },
            });

            for (auto const& mod : mods)
            {
                doInvariantCheck(
                    {{"changed an unchangeable field"}},
                    [&](Account const& a1, Account const&, ApplyContext& ac) {
                        auto sle = ac.view().peek(keylet::account(a1.id()));
                        if (!sle)
                            return false;
                        mod(sle);
                        ac.view().update(sle);
                        return true;
                    });
            }
        }
    }

    void
    testValidLoanBroker()
    {
        testcase << "valid loan broker";

        using namespace jtx;

        enum class Asset { XRP, IOU, MPT };
        auto const assetTypes = std::to_array({Asset::XRP, Asset::IOU, Asset::MPT});

        for (auto const assetType : assetTypes)
        {
            // Initialize with a placeholder value because there's no default
            // ctor
            auto const setupAsset =
                [&](Account const& alice, Account const& issuer, Env& env) -> PrettyAsset {
                switch (assetType)
                {
                    case Asset::IOU: {
                        PrettyAsset const iouAsset = issuer["IOU"];
                        env(trust(alice, iouAsset(1000)));
                        env(pay(issuer, alice, iouAsset(1000)));
                        env.close();
                        return iouAsset;
                    }
                    case Asset::MPT: {
                        MPTTester mptt{env, issuer, kMptInitNoFund};
                        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
                        PrettyAsset const mptAsset = mptt.issuanceID();
                        mptt.authorize({.account = alice});
                        env(pay(issuer, alice, mptAsset(1000)));
                        env.close();
                        return mptAsset;
                    }
                    case Asset::XRP:
                    default:
                        return PrettyAsset{xrpIssue(), 1'000'000};
                }
            };

            Keylet loanBrokerKeylet = keylet::amendments();
            Preclose const createLoanBroker =
                [&, this](Account const& alice, Account const& issuer, Env& env) {
                    auto const asset = setupAsset(alice, issuer, env);
                    loanBrokerKeylet = this->createLoanBroker(alice, env, asset);
                    return BEAST_EXPECT(env.le(loanBrokerKeylet));
                };

            // Ensure the test scenarios are set up completely. The test cases
            // will need to recompute any of these values it needs for itself
            // rather than trying to return a bunch of items
            auto setupTest = [&, this](Account const& a1, Account const&, ApplyContext& ac)
                -> std::optional<std::pair<SLE::pointer, SLE::pointer>> {
                if (loanBrokerKeylet.type != ltLOAN_BROKER)
                    return {};
                auto sleBroker = ac.view().peek(loanBrokerKeylet);
                if (!sleBroker)
                    return {};
                if (!BEAST_EXPECT(sleBroker->at(sfOwnerCount) == 0))
                    return {};
                // Need to touch sleBroker so that it is included in the
                // modified entries for the invariant to find
                ac.view().update(sleBroker);

                // The pseudo-account holds the directory, so get it
                auto const pseudoAccountID = sleBroker->at(sfAccount);
                auto const pseudoAccountKeylet = keylet::account(pseudoAccountID);
                // Strictly speaking, we don't need to load the
                // ACCOUNT_ROOT, but check anyway
                auto slePseudo = ac.view().peek(pseudoAccountKeylet);
                if (!BEAST_EXPECT(slePseudo))
                    return {};
                // Make sure the directory doesn't already exist
                auto const dirKeylet = keylet::ownerDir(pseudoAccountID);
                auto sleDir = ac.view().peek(dirKeylet);
                auto const describe = describeOwnerDir(pseudoAccountID);
                if (!sleDir)
                {
                    // Create the directory
                    BEAST_EXPECT(
                        ::xrpl::directory::createRoot(
                            ac.view(), dirKeylet, loanBrokerKeylet.key, describe) == 0);

                    sleDir = ac.view().peek(dirKeylet);
                }

                return std::make_pair(slePseudo, sleDir);
            };

            doInvariantCheck(
                {{"Loan Broker with zero OwnerCount has multiple directory "
                  "pages"}},
                [&setupTest, this](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto test = setupTest(a1, a2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto const describe = describeOwnerDir(slePseudo->at(sfAccount));

                    BEAST_EXPECT(
                        ::xrpl::directory::insertPage(
                            ac.view(),
                            0,
                            sleDir,
                            0,
                            sleDir,
                            slePseudo->key(),
                            keylet::page(sleDir->key(), 0),
                            describe) == 1);

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker with zero OwnerCount has multiple indexes in "
                  "the Directory root"}},
                [&setupTest](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto test = setupTest(a1, a2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto indexes = sleDir->getFieldV256(sfIndexes);

                    // Put some extra garbage into the directory
                    for (auto const& key : {slePseudo->key(), sleDir->key()})
                    {
                        ::xrpl::directory::insertKey(ac.view(), sleDir, 0, false, indexes, key);
                    }

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker directory corrupt"}},
                [&setupTest](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto test = setupTest(a1, a2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto const describe = describeOwnerDir(slePseudo->at(sfAccount));
                    // Empty vector will overwrite the existing entry for the
                    // holding, if any, avoiding the "has multiple indexes"
                    // failure.
                    STVector256 indexes;

                    // Put one meaningless key into the directory
                    auto const key = keylet::account(Account("random").id()).key;
                    ::xrpl::directory::insertKey(ac.view(), sleDir, 0, false, indexes, key);

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker with zero OwnerCount has an unexpected entry in "
                  "the directory"}},
                [&setupTest](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto test = setupTest(a1, a2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    // Empty vector will overwrite the existing entry for the
                    // holding, if any, avoiding the "has multiple indexes"
                    // failure.
                    STVector256 indexes;

                    ::xrpl::directory::insertKey(
                        ac.view(), sleDir, 0, false, indexes, slePseudo->key());

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker sequence number decreased"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    if (loanBrokerKeylet.type != ltLOAN_BROKER)
                        return false;
                    auto sleBroker = ac.view().peek(loanBrokerKeylet);
                    if (!sleBroker)
                        return false;
                    if (!BEAST_EXPECT(sleBroker->at(sfLoanSequence) > 0))
                        return false;
                    // Need to touch sleBroker so that it is included in the
                    // modified entries for the invariant to find
                    ac.view().update(sleBroker);

                    sleBroker->at(sfLoanSequence) -= 1;

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            // Test: cover available less than pseudo-account asset balance
            {
                Keylet brokerKeylet = keylet::amendments();
                Preclose const createBrokerWithCover =
                    [&, this](Account const& alice, Account const& issuer, Env& env) {
                        auto const asset = setupAsset(alice, issuer, env);
                        brokerKeylet = this->createLoanBroker(alice, env, asset);
                        if (!BEAST_EXPECT(env.le(brokerKeylet)))
                            return false;
                        env(loan_broker::coverDeposit(alice, brokerKeylet.key, asset(10)));
                        env.close();
                        return BEAST_EXPECT(env.le(brokerKeylet));
                    };

                doInvariantCheck(
                    {{"Loan Broker cover available is less than pseudo-account asset balance"}},
                    [&](Account const&, Account const&, ApplyContext& ac) {
                        auto sle = ac.view().peek(brokerKeylet);
                        if (!BEAST_EXPECT(sle))
                            return false;
                        // Pseudo-account holds 10 units, set cover to 5
                        sle->at(sfCoverAvailable) = Number(5);
                        ac.view().update(sle);
                        return true;
                    },
                    XRPAmount{},
                    STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                    {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                    createBrokerWithCover);
            }

            // Test: cover available greater than pseudo-account asset balance
            // (requires fixCleanup3_1_3)
            doInvariantCheck(
                {{"Loan Broker cover available is greater than pseudo-account asset balance"}},
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(loanBrokerKeylet);
                    if (!BEAST_EXPECT(sle))
                        return false;
                    // Pseudo-account has no cover deposited; set cover
                    // higher than any incidental balance
                    sle->at(sfCoverAvailable) = Number(1'000'000);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);
        }
    }

    void
    testVault()  // NOLINT(readability-function-size)
    {
        using namespace test::jtx;

        struct AccountAmount
        {
            AccountID account;
            int amount;
        };
        // Parameters for a synthetic loan object created alongside a vault
        // adjustment. The interest due booked to the vault is
        // totalValueOutstanding - principalOutstanding - managementFeeOutstanding.
        struct LoanParams
        {
            int principalOutstanding = 0;
            int totalValueOutstanding = 0;
            int managementFeeOutstanding = 0;
            AccountID borrower = beast::kZero;
        };
        struct Adjustments
        {
            // NOLINTBEGIN(readability-redundant-member-init)
            std::optional<int> assetsTotal = std::nullopt;
            std::optional<int> assetsAvailable = std::nullopt;
            std::optional<int> lossUnrealized = std::nullopt;
            std::optional<int> assetsMaximum = std::nullopt;
            std::optional<int> sharesTotal = std::nullopt;
            std::optional<int> vaultAssets = std::nullopt;
            std::optional<AccountAmount> accountAssets = std::nullopt;
            std::optional<AccountAmount> accountShares = std::nullopt;
            std::optional<LoanParams> createLoan = std::nullopt;
            // Number of loan objects to create (only used when createLoan is
            // set); a valid loan set creates exactly one.
            int loanCount = 1;
            // NOLINTEND(readability-redundant-member-init)
        };
        constexpr auto kAdjust = [&](ApplyView& ac, xrpl::Keylet keylet, Adjustments args) {
            auto sleVault = ac.peek(keylet);
            if (!sleVault)
                return false;

            auto const mptIssuanceID = (*sleVault)[sfShareMPTID];
            auto sleShares = ac.peek(keylet::mptokenIssuance(mptIssuanceID));
            if (!sleShares)
                return false;

            // These two fields are adjusted in absolute terms
            if (args.lossUnrealized)
                (*sleVault)[sfLossUnrealized] = *args.lossUnrealized;
            if (args.assetsMaximum)
                (*sleVault)[sfAssetsMaximum] = *args.assetsMaximum;

            // Remaining fields are adjusted in terms of difference
            if (args.assetsTotal)
                (*sleVault)[sfAssetsTotal] = *(*sleVault)[sfAssetsTotal] + *args.assetsTotal;
            if (args.assetsAvailable)
            {
                (*sleVault)[sfAssetsAvailable] =
                    *(*sleVault)[sfAssetsAvailable] + *args.assetsAvailable;
            }
            ac.update(sleVault);

            if (args.sharesTotal)
            {
                (*sleShares)[sfOutstandingAmount] =
                    *(*sleShares)[sfOutstandingAmount] + *args.sharesTotal;
                ac.update(sleShares);
            }

            auto const assets = *(*sleVault)[sfAsset];
            auto const pseudoId = *(*sleVault)[sfAccount];
            if (args.vaultAssets)
            {
                if (assets.native())
                {
                    auto slePseudoAccount = ac.peek(keylet::account(pseudoId));
                    if (!slePseudoAccount)
                        return false;
                    (*slePseudoAccount)[sfBalance] =
                        *(*slePseudoAccount)[sfBalance] + *args.vaultAssets;
                    ac.update(slePseudoAccount);
                }
                else if (assets.holds<MPTIssue>())
                {
                    auto const mptId = assets.get<MPTIssue>().getMptID();
                    auto sleMPToken = ac.peek(keylet::mptoken(mptId, pseudoId));
                    if (!sleMPToken)
                        return false;
                    (*sleMPToken)[sfMPTAmount] = *(*sleMPToken)[sfMPTAmount] + *args.vaultAssets;
                    ac.update(sleMPToken);
                }
                else
                {
                    return false;  // Not supporting testing with IOU
                }
            }

            if (args.accountAssets)
            {
                auto const& pair = *args.accountAssets;
                if (assets.native())
                {
                    auto sleAccount = ac.peek(keylet::account(pair.account));
                    if (!sleAccount)
                        return false;
                    (*sleAccount)[sfBalance] = *(*sleAccount)[sfBalance] + pair.amount;
                    ac.update(sleAccount);
                }
                else if (assets.holds<MPTIssue>())
                {
                    auto const mptID = assets.get<MPTIssue>().getMptID();
                    auto sleMPToken = ac.peek(keylet::mptoken(mptID, pair.account));
                    if (!sleMPToken)
                        return false;
                    (*sleMPToken)[sfMPTAmount] = *(*sleMPToken)[sfMPTAmount] + pair.amount;
                    ac.update(sleMPToken);
                }
                else
                {
                    return false;  // Not supporting testing with IOU
                }
            }

            if (args.accountShares)
            {
                auto const& pair = *args.accountShares;
                auto sleMPToken = ac.peek(keylet::mptoken(mptIssuanceID, pair.account));
                if (!sleMPToken)
                    return false;
                (*sleMPToken)[sfMPTAmount] = *(*sleMPToken)[sfMPTAmount] + pair.amount;
                ac.update(sleMPToken);
            }

            if (args.createLoan)
            {
                auto const& lp = *args.createLoan;
                bool const anyOutstanding = lp.principalOutstanding != 0 ||
                    lp.totalValueOutstanding != 0 || lp.managementFeeOutstanding != 0;
                for (std::uint32_t seq = 1; seq <= static_cast<std::uint32_t>(args.loanCount);
                     ++seq)
                {
                    auto sleLoan =
                        std::make_shared<SLE>(keylet::loan(keylet.key, SeqProxy::rawSequence(seq)));
                    sleLoan->at(sfPrincipalOutstanding) = Number(lp.principalOutstanding);
                    sleLoan->at(sfTotalValueOutstanding) = Number(lp.totalValueOutstanding);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(lp.managementFeeOutstanding);
                    // ValidLoan requires a positive periodic payment, and that a
                    // loan with payments remaining is not fully paid off.
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, anyOutstanding ? 1 : 0);
                    sleLoan->setAccountID(sfBorrower, lp.borrower);
                    sleLoan->makeFieldPresent(sfOwnerNode);
                    ac.insert(sleLoan);
                }
            }
            return true;
        };

        static constexpr auto kArgs = [](AccountID id, int adjustment, auto fn) -> Adjustments {
            Adjustments sample = {
                .assetsTotal = adjustment,
                .assetsAvailable = adjustment,
                .lossUnrealized = 0,
                .sharesTotal = adjustment,
                .vaultAssets = adjustment,
                .accountAssets =  //
                AccountAmount{.account = id, .amount = -adjustment},
                .accountShares =  //
                AccountAmount{.account = id, .amount = adjustment}};
            fn(sample);
            return sample;
        };

        Account const a3{"A3"};
        Account const a4{"A4"};
        auto const precloseXrp = [&](Account const& a1, Account const& a2, Env& env) -> bool {
            env.fund(XRP(1000), a3, a4);
            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
            env(tx);
            env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a2, .id = keylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a3, .id = keylet.key, .amount = XRP(10)}));
            return true;
        };

        testcase << "Vault general checks";
        doInvariantCheck(
            {"vault deletion succeeded without deleting a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type",
             "deleted Vault without deleting its pseudo-account"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(sequence));
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);
                sleVault->setAccountID(sfAccount, a1.id());
                ac.view().insert(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {"vault deleted by a wrong transaction type",
             "deleted Vault without deleting its pseudo-account"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation updated more than single vault",
             "deleted Vault without deleting its pseudo-account"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                {
                    auto const keylet =
                        keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                    auto sleVault = ac.view().peek(keylet);
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);
                }
                {
                    auto const keylet =
                        keylet::vault(a2.id(), SeqProxy::rawSequence(ac.view().seq()));
                    auto sleVault = ac.view().peek(keylet);
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);
                }
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                {
                    auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                    env(tx);
                }
                {
                    auto [tx, _] = vault.create({.owner = a2, .asset = xrpIssue()});
                    env(tx);
                }
                return true;
            });

        doInvariantCheck(
            {"vault operation updated more than single vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const insertVault = [&](Account const a) {
                    auto const vaultKeylet = keylet::vault(a.id(), SeqProxy::rawSequence(sequence));
                    auto sleVault = std::make_shared<SLE>(vaultKeylet);
                    auto const vaultPage = ac.view().dirInsert(
                        keylet::ownerDir(a.id()), sleVault->key(), describeOwnerDir(a.id()));
                    sleVault->setFieldU64(sfOwnerNode, *vaultPage);
                    sleVault->setAccountID(sfAccount, a.id());
                    ac.view().insert(sleVault);
                };
                insertVault(a1);
                insertVault(a2);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {"deleted vault must also delete shares",
             "deleted Vault without deleting its pseudo-account"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"deleted vault must have no shares outstanding",
             "deleted vault must have no assets outstanding",
             "deleted vault must have no assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().erase(sleVault);
                ac.view().erase(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                // Note, such an "orphaned" update of MPT issuance attached to a
                // vault is invalid; ttVAULT_SET must also update Vault object.
                sleShares->setFieldH256(sfDomainID, uint256(13));
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"updated vault must have shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsMaximum] = 200;
                ac.view().update(sleVault);

                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().erase(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without updating shares",
             "assets available must not be greater than assets outstanding"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsTotal] = 9;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
                return true;
            });

        doInvariantCheck(
            {"set must not change assets outstanding",
             "set must not change assets available",
             "set must not change shares outstanding",
             "set must not change vault balance",
             "assets available must not be negative",
             "assets available must not be greater than assets outstanding",
             "assets outstanding must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto slePseudoAccount = ac.view().peek(keylet::account(*(*sleVault)[sfAccount]));
                if (!slePseudoAccount)
                    return false;
                (*slePseudoAccount)[sfBalance] = *(*slePseudoAccount)[sfBalance] - 10;
                ac.view().update(slePseudoAccount);

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(a4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.assetsAvailable = (kDropsPerXrp * -100).value();
                                   sample.assetsTotal = (kDropsPerXrp * -200).value();
                                   sample.sharesTotal = -1;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // Under featureLendingProtocolV1_1 vault immutability is enforced by
        // NoModifiedUnmodifiableFields (class-1, both passes), which reports
        // "changed an unchangeable field" and escalates to tef on pass 2.
        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldIssue(sfAsset, STIssue{sfAsset, MPTIssue(MPTID(42))});
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setAccountID(sfAccount, a2.id());
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfShareMPTID] = MPTID(42);
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // Pre-featureLendingProtocolV1_1 the immutability of sfAsset, sfAccount
        // and sfShareMPTID is enforced by ValidVault directly, which reports
        // "violation of vault immutable data" on the first pass. ValidVault
        // returns early on the second pass (result already tec), so the check
        // does not escalate to tef. Once V1_1 activates, the same fields are
        // covered by NoModifiedUnmodifiableFields (see the three cases above);
        // the two paths are mutually exclusive so both need coverage.
        auto const preLendingV11Amendments = defaultAmendments() - featureLendingProtocolV1_1;
        doInvariantCheck(
            makeEnv(preLendingV11Amendments),
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldIssue(sfAsset, STIssue{sfAsset, MPTIssue(MPTID(42))});
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            makeEnv(preLendingV11Amendments),
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setAccountID(sfAccount, a2.id());
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            makeEnv(preLendingV11Amendments),
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfShareMPTID] = MPTID(42);
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"vault transaction must not change loss unrealized",
             "set must not change assets outstanding"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.lossUnrealized = 13;
                                   sample.assetsTotal = 20;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"loss unrealized must not exceed the difference "
             "between assets outstanding and available",
             "vault transaction must not change loss unrealized"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 100, [&](Adjustments& sample) {
                                   sample.lossUnrealized = 13;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // A negative loss unrealized must trip the invariant. ttLOAN_MANAGE is
        // allowed to change loss unrealized, so it isolates this check from the
        // "must not change loss unrealized" invariant. Gated behind
        // fixCleanup3_4_0 (see below).
        doInvariantCheck(
            {"loss unrealized must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.lossUnrealized = -1;
                               }));
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // Without fixCleanup3_4_0 the same state must NOT trip the invariant,
        // preserving pre-amendment behavior (no fork risk). Also remove
        // featureLendingProtocolV1_1 so finalizeLoanManage's stricter checks
        // (exactly one loan touched) do not fire from a bare vault mutation
        // that does not touch a loan.
        doInvariantCheck(
            makeEnv(defaultAmendments() - fixCleanup3_4_0 - featureLendingProtocolV1_1),
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.lossUnrealized = -1;
                               }));
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) {}},
            {tesSUCCESS, tesSUCCESS},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"set assets outstanding must not exceed assets maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.assetsMaximum = 1;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"assets maximum must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.assetsMaximum = -1;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"set must not change shares outstanding",
             "updated zero sized vault must have no assets outstanding",
             "updated zero sized vault must have no assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfOutstandingAmount] = 0;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfMaximumAmount] = 10;
                ac.view().update(sleShares);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments&) {}));

                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfOutstandingAmount] = kMaxMpTokenAmount + 1;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        testcase << "Vault loan operations";

        // ttLOAN_MANAGE (impair): assets outstanding must not change. Only
        // assets outstanding is bumped, so the common checks (vault balance,
        // assets available, shares) all pass and only the impair/unimpair
        // sub-check fires.
        doInvariantCheck(
            {"loan impair/unimpair must not change assets outstanding"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.assetsTotal = 100});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanImpair); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE with none of the sub-operation flags (impair,
        // unimpair, default) is a no-op and must not modify the vault.
        doInvariantCheck(
            {"loan manage without a sub-operation must not modify the vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.assetsTotal = 100});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: exactly one loan is created, but the vault
        // (pseudo-account) balance does not change, so only the balance check
        // trips. A loan must be created so the "exactly one loan" check passes
        // first and the balance check is actually reached.
        doInvariantCheck(
            {"loan set must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .createLoan = LoanParams{
                            .principalOutstanding = 200,
                            .totalValueOutstanding = 200,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: the balance decreases, but not by the principal requested
        doInvariantCheck(
            {"loan set must decrease vault balance by the principal requested"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100},
                        .createLoan = LoanParams{
                            .principalOutstanding = 200,
                            .totalValueOutstanding = 200,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{
                ttLOAN_SET,
                [](STObject& tx) {
                    tx.at(sfPrincipalRequested) = Number(200);
                    tx.makeFieldPresent(sfCounterpartySignature);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: vault balance decreases by the principal requested, but
        // assets available decreases by a different amount, so only the shared
        // "vault balance and assets available" check trips.
        doInvariantCheck(
            {"vault balance and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -150,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .createLoan = LoanParams{
                            .principalOutstanding = 200,
                            .totalValueOutstanding = 200,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{
                ttLOAN_SET,
                [](STObject& tx) {
                    tx.at(sfPrincipalRequested) = Number(200);
                    tx.makeFieldPresent(sfCounterpartySignature);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: principal matches, but no loan object is created
        doInvariantCheck(
            {"lending transaction must touch exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200}});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: principal matches, but more than one loan is created
        doInvariantCheck(
            {"lending transaction must touch exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .createLoan =
                            LoanParams{
                                .principalOutstanding = 100,
                                .totalValueOutstanding = 100,
                                .borrower = a1.id(),
                            },
                        .loanCount = 2});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: no new loan is created, but an existing loan is modified
        // instead. The cardinality helper distinguishes create from modify;
        // a set that touches a pre-existing loan is spurious.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            // Pre-existing loan in the base view; modifying it in the apply
            // view is what the cardinality check must reject for a set.
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_SET, [](STObject& t) { t.at(sfPrincipalRequested) = Number(200); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Move the vault balance and available assets to match the
            // principal requested, so the funding checks pass and only the
            // cardinality shape is left to trip.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200}})))
                return;
            // Modify the pre-existing loan so afterLoan_ has one entry with a
            // non-empty beforeLoan_ counterpart: this is the "modify" shape
            // that a set transaction must never produce.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(50);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "lending transaction must not modify an existing loan"));
        }

        // ttLOAN_SET: principal matches, but shares outstanding changes
        doInvariantCheck(
            {"shares outstanding must only change by deposit, withdraw, or clawback"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .sharesTotal = 10,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .accountShares = AccountAmount{.account = a2.id(), .amount = 10},
                        .createLoan = LoanParams{
                            .principalOutstanding = 200,
                            .totalValueOutstanding = 200,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{
                ttLOAN_SET,
                [](STObject& tx) {
                    tx.at(sfPrincipalRequested) = Number(200);
                    tx.makeFieldPresent(sfCounterpartySignature);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: everything balances (principal released, exactly one loan
        // created booking zero interest, shares untouched, assets outstanding
        // unchanged), but the created loan records a principal outstanding that
        // differs from the principal requested. The vault only released 200 to
        // the borrower, yet the loan claims 300 principal, decoupling the
        // borrower's debt from the assets actually lent and enabling share-price
        // manipulation on repayment.
        doInvariantCheck(
            {"loan set principal outstanding must equal principal requested"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .createLoan = LoanParams{
                            .principalOutstanding = 300,
                            .totalValueOutstanding = 300,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET pre-featureLendingProtocolV1_1: finalizeLoanSet short-
        // circuits and returns success without inspecting the loan or the
        // vault. The same state that trips the principal-outstanding check
        // under V1_1 must be silently accepted here.
        doInvariantCheck(
            makeEnv(defaultAmendments() - featureLendingProtocolV1_1),
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .createLoan = LoanParams{
                            .principalOutstanding = 300,
                            .totalValueOutstanding = 300,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tesSUCCESS, tesSUCCESS},
            precloseXrp);

        // ttLOAN_SET: the loan's broker must record the newly-originated
        // debt. Vault cash flows and loan shape are correct; the broker is
        // touched (so the "modify exactly the loan's broker" cardinality
        // check passes) but its sfDebtTotal is left unchanged, so
        // `Δ DebtTotal (0) != ownedToVault (100)` and the residual check
        // trips. Requires a real broker on the ledger, so the setup is
        // bespoke.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            // Fund the vault so it can release the principal.
            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            STTx const tx{ttLOAN_SET, [&](STObject& t) {
                              t.at(sfPrincipalRequested) = Number(100);
                              t.at(sfLoanBrokerID) = brokerKeylet.key;
                          }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Move principal (100) out of the vault to the borrower a2 with
            // matching book-keeping so the earlier funding-side checks pass.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a2.id(),
                        }})))
                return;

            // Point the loan at the real broker so afterBroker_[0].key ==
            // loan.loanBrokerID, otherwise the earlier cardinality check
            // trips first.
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                ac.view().update(sleLoan);
            }

            // Touch the broker so before/afterBroker_ snapshots are captured
            // with matching keys, but leave sfDebtTotal at its base value.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan set must increase broker debt total by the amount the "
                "new loan owes to the vault"));
        }

        // ttLOAN_SET: the borrower must receive the requested principal (net
        // of origination fee). Vault flows, DebtTotal and the loan shape all
        // balance, but the borrower is credited with only half the principal
        // and the remainder is routed to an unrelated account. The vault-side
        // identity still holds (assetsTotal/assetsAvailable/ownedToVault
        // balance), so only the borrower-disburse check fires.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2, a3);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            STTx const tx{ttLOAN_SET, [&](STObject& t) {
                              t.at(sfPrincipalRequested) = Number(100);
                              t.at(sfLoanBrokerID) = brokerKeylet.key;
                          }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault releases 100 (correct principal); the borrower only sees
            // 50 of it, the other 50 is dropped on a3. The vault-side
            // identity depends only on the vault, so it still holds.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 50},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a2.id(),
                        }})))
                return;

            // Absorb the remaining 50 on an unrelated account so the vault
            // ledger stays whole while the borrower is short-changed.
            {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!BEAST_EXPECT(sleA3))
                    return;
                sleA3->at(sfBalance) = *sleA3->at(sfBalance) + 50;
                ac.view().update(sleA3);
            }

            // Point the loan at the real broker.
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                ac.view().update(sleLoan);
            }

            // Broker's DebtTotal must grow by ownedToVault (100) so the
            // earlier DebtTotal check passes and the borrower-disburse
            // check is the one that trips.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) + Number(100);
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan set must credit the borrower with the principal net of "
                "origination fee"));
        }

        // ttLOAN_SET: when the loan carries a non-zero origination fee, the
        // broker owner must be credited with that fee. Vault flows, DebtTotal
        // and the borrower-disburse (principal net of fee) all balance, but
        // the fee is dropped on an unrelated account instead of the broker
        // owner, so only the broker-owner-disburse check fires.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2, a3);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            STTx const tx{ttLOAN_SET, [&](STObject& t) {
                              t.at(sfPrincipalRequested) = Number(100);
                              t.at(sfLoanBrokerID) = brokerKeylet.key;
                          }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault releases 100; borrower receives principal net of the
            // 10-unit origination fee, so a2 gets 90 (the correct amount).
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 90},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a2.id(),
                        }})))
                return;

            // Route the origination fee to an unrelated account so the
            // broker owner (a1) delta stays at zero.
            {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!BEAST_EXPECT(sleA3))
                    return;
                sleA3->at(sfBalance) = *sleA3->at(sfBalance) + 10;
                ac.view().update(sleA3);
            }

            // Point the loan at the real broker and stamp the origination
            // fee so the borrower-net-of-fee expected value is 90.
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfLoanOriginationFee) = Number(10);
                ac.view().update(sleLoan);
            }

            // ownedToVault = 100 (totalValueOutstanding), so bump DebtTotal
            // by 100 to keep the earlier DebtTotal check happy.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) + Number(100);
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan set must credit the broker owner with the origination fee"));
        }

        // ttLOAN_SET: vault-side accounting identity
        // `Δ AssetsTotal − Δ AssetsAvailable == loan.ownedToVault(version)`.
        // Vault balance and assets available fall by 100 (matching the
        // principal release) but assets total is bumped by an extra 10, so
        // the residual is 10 rather than zero. Every other check (funding,
        // borrower/broker owner disburse, DebtTotal, cardinality) passes.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            STTx const tx{ttLOAN_SET, [&](STObject& t) {
                              t.at(sfPrincipalRequested) = Number(100);
                              t.at(sfLoanBrokerID) = brokerKeylet.key;
                          }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault releases 100 (both vault balance and assetsAvailable
            // drop by 100), but assetsTotal is bumped by an extra 10 -
            // interest booking with no corresponding loan-side obligation.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsTotal = 10,
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a2.id(),
                        }})))
                return;

            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                ac.view().update(sleLoan);
            }

            // Bump DebtTotal by ownedToVault (100) so the earlier DebtTotal
            // check passes.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) + Number(100);
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan set assets outstanding must match the principal "
                "released and the amount the new loan owes to the vault"));
        }

        // ttLOAN_MANAGE: no loan is touched at all. Every lending transaction
        // must operate on exactly one loan; a manage with none is spurious.
        doInvariantCheck(
            {"lending transaction must touch exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanImpair); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE: a loan is created rather than modified. The cardinality
        // helper distinguishes create from modify and rejects the wrong shape.
        doInvariantCheck(
            {"lending transaction must modify exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanImpair); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE: vault balance and assets available do not add up
        doInvariantCheck(
            {"vault balance and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.assetsAvailable = -100});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE: loss unrealized driven negative
        doInvariantCheck(
            {"loss unrealized must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.lossUnrealized = -1});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE: shares outstanding changes
        //
        // ValidMPTPayment enforces its OutstandingAmount balance identity
        // regardless of TER under featureLendingProtocolV1_1, and the harness runs both
        // invariant passes against the same view (no reset in between), so
        // visitEntry accumulates the MPT delta on the second pass and trips
        // ValidMPTPayment alongside the share-change check -> escalation
        // to tef. Real production always resets between passes.
        doInvariantCheck(
            {"shares outstanding must only change by deposit, withdraw, or clawback"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .sharesTotal = 10,
                        .accountShares = AccountAmount{.account = a2.id(), .amount = 10}});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (impair): assets available must not change
        doInvariantCheck(
            {"loan impair/unimpair must not change assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100}});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanImpair); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (default): assets available must not decrease
        doInvariantCheck(
            {"loan default must not decrease assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = -100,
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100}});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanDefault); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (default): assets outstanding must not increase
        doInvariantCheck(
            {"loan default must not increase assets outstanding"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 100,
                        .assetsAvailable = 100,
                        .vaultAssets = 100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -100}});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanDefault); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (unimpair): loss unrealized must not increase. Bumping
        // loss unrealized upward is the wrong direction for unimpair, which
        // reverses a paper loss.
        doInvariantCheck(
            {"loan impair must not decrease, and loan unimpair must not "
             "increase, loss unrealized"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.lossUnrealized = 5});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanUnimpair); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (impair): loss unrealized must not decrease. The base
        // ledger cannot express a nonzero prior lossUnrealized through the
        // shared harness, so the setup is bespoke: seed the vault with a small
        // paper loss and then drop it back to zero under an impair — the
        // wrong direction for impair, which only ever grows the paper loss.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            // Seed the vault with a paper loss in the base view so a
            // decrease in the apply view registers as a negative delta. The
            // SLE is cloned so the base and apply views hold separate copies.
            {
                auto const sleVaultRead = ov.read(vaultKeylet);
                if (!BEAST_EXPECT(sleVaultRead))
                    return;
                auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                sleVault->at(sfLossUnrealized) = Number(5);
                ov.rawReplace(sleVault);
            }

            STTx const tx{ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanImpair); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Reset lossUnrealized to zero under an impair: after (0) < before
            // (5), so the delta is negative and the sign check must reject.
            auto sleVault = ac.view().peek(vaultKeylet);
            if (!BEAST_EXPECT(sleVault))
                return;
            sleVault->at(sfLossUnrealized) = Number(0);
            ac.view().update(sleVault);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan impair must not decrease, and loan unimpair must not "
                "increase, loss unrealized"));
        }

        // ttLOAN_MANAGE (impair/unimpair): loss unrealized must move by
        // exactly the amount the loan owes to the vault. Both directions are
        // exercised: impair grows the paper loss, unimpair shrinks it. The
        // bookkeeping is off by 50 (owed is 100, delta is 50), so the sign
        // check at loan-manage-common passes but the magnitude check trips.
        {
            struct Case
            {
                std::uint32_t txFlag;
                std::uint32_t beforeLoanFlags;
                std::uint32_t afterLoanFlags;
                Number beforeLossUnrealized;
                Number afterLossUnrealized;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                {.txFlag = tfLoanImpair,
                 .beforeLoanFlags = 0,
                 .afterLoanFlags = lsfLoanImpaired,
                 .beforeLossUnrealized = Number(0),
                 .afterLossUnrealized = Number(50),
                 .expected =
                     "loan impair must increase loss unrealized by exactly the amount the loan "
                     "owes to the vault"},
                {.txFlag = tfLoanUnimpair,
                 .beforeLoanFlags = lsfLoanImpaired,
                 .afterLoanFlags = 0,
                 .beforeLossUnrealized = Number(100),
                 .afterLossUnrealized = Number(50),
                 .expected =
                     "loan unimpair must decrease loss unrealized by exactly the amount the loan "
                     "owes to the vault"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, defaultAmendments()};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
                // Seed the vault with the initial lossUnrealized so the
                // apply-view mutation registers as a bounded delta.
                {
                    auto const sleVaultRead = ov.read(vaultKeylet);
                    if (!BEAST_EXPECT(sleVaultRead))
                        continue;
                    auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                    sleVault->at(sfLossUnrealized) = c.beforeLossUnrealized;
                    ov.rawReplace(sleVault);
                }

                // Seed a loan in the base view with `ownedToVault` == 100 so
                // the magnitude check has a definite target.
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(100);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, c.beforeLoanFlags);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_MANAGE, [&](STObject& t) { t.setFieldU32(sfFlags, c.txFlag); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                // Modify lossUnrealized by the wrong delta (50 instead of 100).
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    continue;
                sleVault->at(sfLossUnrealized) = c.afterLossUnrealized;
                ac.view().update(sleVault);

                // Flip the impair flag on the loan so the flag-transition
                // check passes and the magnitude check is reached.
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, c.afterLoanFlags);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(c.expected));
            }
        }

        // ttLOAN_MANAGE (impair): a successful impair must leave the loan
        // flagged as impaired and must not target an already-impaired loan.
        // Each violation is set up bespoke because the shared harness cannot
        // seed a pre-existing loan whose lsfLoanImpaired bit we can control
        // across the before/after boundary.
        {
            struct Case
            {
                std::uint32_t before;
                std::uint32_t after;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                // Impair a non-impaired loan but forget to set the flag.
                {.before = 0,
                 .after = 0,
                 .expected =
                     "LoanManage(tfLoanImpair) must set lsfLoanImpaired on a non-impaired loan"},
                // Impair an already-impaired loan (regardless of the resulting flag).
                {.before = lsfLoanImpaired,
                 .after = lsfLoanImpaired,
                 .expected =
                     "LoanManage(tfLoanImpair) must set lsfLoanImpaired on a non-impaired loan"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, defaultAmendments()};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, c.before);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanImpair); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, c.after);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(c.expected));
            }
        }

        // ttLOAN_MANAGE (unimpair): the mirror of the impair check. A
        // successful unimpair must leave the loan without lsfLoanImpaired and
        // must not target a non-impaired loan. Setup mirrors the impair block
        // above.
        {
            struct Case
            {
                std::uint32_t before;
                std::uint32_t after;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                // Unimpair an impaired loan but forget to clear the flag.
                {.before = lsfLoanImpaired,
                 .after = lsfLoanImpaired,
                 .expected =
                     "LoanManage(tfLoanUnimpair) must clear lsfLoanImpaired on an impaired loan"},
                // Unimpair a non-impaired loan (regardless of the resulting flag).
                {.before = 0,
                 .after = 0,
                 .expected =
                     "LoanManage(tfLoanUnimpair) must clear lsfLoanImpaired on an impaired loan"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, defaultAmendments()};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, c.before);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanUnimpair); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, c.after);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(c.expected));
            }
        }

        // ttLOAN_MANAGE (default): a defaulted loan atomically enters a
        // terminal state, which drops sfNextPaymentDueDate from the ledger
        // entry. Seed a loan that already carries lsfLoanDefault so the
        // "must newly set" check passes, then leave sfNextPaymentDueDate
        // present and non-zero on the after-image; the residual due-date
        // check must then fire.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            // Pre-insert a loan that is not yet defaulted but has a
            // NextPaymentDueDate set; the apply-view mutation below flips
            // lsfLoanDefault (so the "must newly set" check passes) while
            // leaving the due date behind.
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfNextPaymentDueDate, 123);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "defaulted loan must have zero next payment due date"));
        }

        // ttLOAN_MANAGE (default): loss unrealized must not increase. A default
        // realizes the paper loss (or leaves it at zero for a non-impaired
        // loan); it can never grow it.
        doInvariantCheck(
            {"loan default must not increase loss unrealized"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.lossUnrealized = 5});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanDefault); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: the vault (pseudo-account) balance must change
        doInvariantCheck(
            {"loan pay must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY pre-featureLendingProtocolV1_1: finalizeLoanPay short-
        // circuits and returns success. The same "no vault balance change"
        // state that trips the check under V1_1 must be silently accepted
        // here.
        doInvariantCheck(
            makeEnv(defaultAmendments() - featureLendingProtocolV1_1),
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tesSUCCESS, tesSUCCESS},
            precloseXrp);

        // ttLOAN_PAY: cash is credited to the vault but no loan is touched.
        // The vault-balance check passes because a real inflow was recorded;
        // it is the cardinality helper that must catch the missing loan.
        doInvariantCheck(
            {"lending transaction must touch exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 50,
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(50)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: cash is credited to the vault and a loan is created
        // rather than modified. A payment services an existing loan, so a
        // create is the wrong shape and the cardinality helper must reject
        // it.
        doInvariantCheck(
            {"lending transaction must modify exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 50,
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(50)); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: assets available must track the real vault balance. The
        // vault balance (pseudo-account) grows by 50 but assets available is
        // bumped by 60, so the two no longer add up.
        doInvariantCheck(
            {"vault balance and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = 60,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(100)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: assets available must increase
        doInvariantCheck(
            {"loan pay must increase assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: assets available increases by more than the amount paid
        doInvariantCheck(
            {"loan pay must not increase assets available by more than the amount paid"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 300,
                        .assetsAvailable = 300,
                        .vaultAssets = 300,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -300}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(100)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: shares outstanding changes
        //
        // Escalates to tef for the same harness reason as the ttLOAN_MANAGE
        // shares-change case above: ValidMPTPayment fires on the second
        // pass because visitEntry-accumulated MPT deltas double.
        doInvariantCheck(
            {"shares outstanding must only change by deposit, withdraw, or clawback"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 100,
                        .assetsAvailable = 100,
                        .sharesTotal = 10,
                        .vaultAssets = 100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -100},
                        .accountShares = AccountAmount{.account = a2.id(), .amount = 10}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: loss unrealized driven negative. The cash inflow is
        // valid, but loss unrealized is set below zero.
        doInvariantCheck(
            {"loss unrealized must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 100,
                        .assetsAvailable = 100,
                        .lossUnrealized = -1,
                        .vaultAssets = 100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -100}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: assets outstanding must match the cash received and the
        // change in the paid loan's claim on the vault. A payment only moves
        // value between the vault's available cash and its claim on the loan,
        // so bumping assets outstanding by more than that must be caught. This
        // needs a loan that already exists in the base ledger (so modifying it
        // is seen as a before/after change), which the shared harness cannot
        // set up, hence the bespoke view construction below.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            // Insert a pre-existing loan into the base view; modifying it in the
            // apply view is then seen as a loan modification (before/after).
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(100)); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Cash received: assets available and the vault balance both grow by
            // 60, paid by a2. The paid loan's claim drops by 60 (total value
            // 150 -> 90). Conservation requires assets outstanding to be
            // unchanged, but we bump it by 10 to violate the identity.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsTotal = 10,
                        .assetsAvailable = 60,
                        .vaultAssets = 60,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -60}})))
                return;

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(40);
            sleLoan->at(sfTotalValueOutstanding) = Number(90);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan pay assets outstanding must match the cash received and "
                "the change in the amount the loan owes to the vault"));
            // The pre-inserted loan carries a default (zero) sfLoanBrokerID,
            // which does not resolve to a live broker; the broker-existence
            // check must therefore also fire in the same walk.
            BEAST_EXPECT(sink.messages().str().contains("loan pay loan broker must exist"));
        }

        // ttLOAN_PAY: the vault's claim on the loan may only shrink. A payment
        // pays the loan down, so total value outstanding (net of management
        // fee) can only fall. The bespoke setup mirrors the conservation test
        // above: a pre-existing loan is inserted so modifying it registers as a
        // before/after change.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Cash inflow of 50 (paid by a2), assets outstanding grows by 100
            // to keep the conservation identity honest (Δtotal - Δavailable -
            // Δclaim = 100 - 50 - 50 = 0). Push both principal (100 → 150)
            // and total value (150 → 200): under either accounting basis the
            // vault's claim grows by 50, which the sign check must reject.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsTotal = 100,
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                return;

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(150);
            sleLoan->at(sfTotalValueOutstanding) = Number(200);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan pay must not increase the amount the loan owes to the vault"));
        }

        // ttLOAN_PAY (non-full repayment): NextPaymentDueDate must advance by
        // a positive multiple of PaymentInterval. The earlier "strictly
        // decrease PrincipalOutstanding" and "decrease PaymentRemaining"
        // checks are satisfied so the due-date check is the one that fires.
        // Two failure modes: the due date does not advance at all, and the
        // due date advances by an amount that is not a multiple of
        // PaymentInterval.
        {
            struct Case
            {
                std::uint32_t beforeDue;
                std::uint32_t afterDue;
                std::string label;
            };
            auto const cases = std::to_array<Case>({
                {.beforeDue = 1000, .afterDue = 1000, .label = "no advance"},
                {.beforeDue = 1000, .afterDue = 1005, .label = "non-multiple advance"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, defaultAmendments()};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 2);
                    sleLoan->setFieldU32(sfPaymentInterval, 10);
                    sleLoan->setFieldU32(sfNextPaymentDueDate, c.beforeDue);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                // Cash inflow of 50 with matching bookkeeping so the earlier
                // conservation checks pass and the due-date check is reached.
                if (!BEAST_EXPECT(kAdjust(
                        ac.view(),
                        vaultKeylet,
                        Adjustments{
                            .assetsTotal = 0,
                            .assetsAvailable = 50,
                            .vaultAssets = 50,
                            .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                    continue;

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                // Strictly decrease principal and payments-remaining so the
                // earlier ttLOAN_PAY checks pass.
                sleLoan->at(sfPrincipalOutstanding) = Number(50);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                sleLoan->setFieldU32(sfNextPaymentDueDate, c.afterDue);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(
                    "loan pay must advance NextPaymentDueDate by a positive "
                    "multiple of PaymentInterval on a non-full-repayment"));
            }
        }

        // ttLOAN_PAY: the vault, the loan-broker pseudo-account and the
        // loan-broker owner are the only three destinations of a payment; their
        // combined inflow can never exceed tx[sfAmount].  Set up a real loan
        // broker so we can address its pseudo-account, then dispense 60 to the
        // vault and 50 to the broker pseudo — a total of 110 for a tx[sfAmount]
        // of 100, tripping the check.  The setup is bespoke because the shared
        // harness does not create loan brokers.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Insert a pre-existing loan pointing at the real broker so
            // finalizeLoanPay's broker lookup succeeds and the inflow-sum
            // check is actually reached.
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(100)); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault balance and assetsAvailable both +60, borrower -110 (60 to
            // vault, 50 to broker pseudo).  The loan's claim drops by 60
            // (total value 150 -> 90) so assets outstanding stays at zero
            // delta and the assets-outstanding-vs-claim identity holds.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = 60,
                        .vaultAssets = 60,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -110}})))
                return;

            auto sleBrokerPseudo = ac.view().peek(keylet::account(brokerPseudoId));
            if (!BEAST_EXPECT(sleBrokerPseudo))
                return;
            (*sleBrokerPseudo)[sfBalance] = *(*sleBrokerPseudo)[sfBalance] + 50;
            ac.view().update(sleBrokerPseudo);

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(40);
            sleLoan->at(sfTotalValueOutstanding) = Number(90);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan pay vault and broker must not receive more than the amount paid"));
        }

        // ttLOAN_PAY: LossUnrealized must fall by exactly the pre-transaction
        // amount the loan owed to the vault when the loan was impaired (a
        // successful payment implicitly unimpairs), or stay unchanged
        // otherwise. Two bespoke cases mirror the impair/unimpair magnitude
        // tests; every other loan-pay identity is lined up so only the
        // LossUnrealized magnitude check trips.
        {
            struct Case
            {
                std::uint32_t beforeLoanFlags;
                Number beforeLossUnrealized;
                Number afterLossUnrealized;
            };
            auto const cases = std::to_array<Case>({
                // Non-impaired: expected Δ is 0, but we drop LossUnrealized
                // by 5.
                {.beforeLoanFlags = 0,
                 .beforeLossUnrealized = Number(5),
                 .afterLossUnrealized = Number(0)},
                // Impaired: the vault is cash-basis, so ownedToVault is the
                // principal only (100) and the expected Δ is -100. We drop
                // LossUnrealized by 50 instead.
                {.beforeLoanFlags = lsfLoanImpaired,
                 .beforeLossUnrealized = Number(150),
                 .afterLossUnrealized = Number(100)},
            });

            for (auto const& c : cases)
            {
                Env env{*this, defaultAmendments()};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                env.close();

                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
                auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
                if (!BEAST_EXPECT(env.le(brokerKeylet)))
                    return;
                env.close();

                auto const sleBrokerBase = env.le(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerBase))
                    return;
                auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

                Vault const vault{env};
                env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
                env.close();

                OpenView ov{*env.current()};

                // Base LossUnrealized and broker DebtTotal reflect an
                // outstanding loan whose ownedToVault is 150 (150 total value
                // less zero management fee); the base broker DebtTotal must
                // match so the Δ DebtTotal identity holds after the payment.
                // AssetsAvailable and pseudoAccount balance are reduced by
                // the same 150 so the vault's own accounting (assetsAvailable
                // + ownedToVault == assetsTotal) is consistent in the base.
                AccountID pseudoId;
                {
                    auto const sleVaultRead = ov.read(vaultKeylet);
                    if (!BEAST_EXPECT(sleVaultRead))
                        continue;
                    pseudoId = sleVaultRead->at(sfAccount);
                    auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                    sleVault->at(sfLossUnrealized) = c.beforeLossUnrealized;
                    sleVault->at(sfAssetsAvailable) =
                        *sleVault->at(sfAssetsAvailable) - Number(150);
                    ov.rawReplace(sleVault);
                }
                {
                    auto const slePseudoRead = ov.read(keylet::account(pseudoId));
                    if (!BEAST_EXPECT(slePseudoRead))
                        continue;
                    auto slePseudo = std::make_shared<SLE>(*slePseudoRead);
                    slePseudo->at(sfBalance) = slePseudo->at(sfBalance) - XRPAmount(150);
                    ov.rawReplace(slePseudo);
                }
                {
                    auto const sleBrokerRead = ov.read(brokerKeylet);
                    if (!BEAST_EXPECT(sleBrokerRead))
                        continue;
                    auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                    sleBroker->at(sfDebtTotal) = Number(150);
                    ov.rawReplace(sleBroker);
                }
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 2);
                    sleLoan->setFieldU32(sfPaymentInterval, 10);
                    sleLoan->setFieldU32(sfNextPaymentDueDate, 1000);
                    sleLoan->setFieldU32(sfFlags, c.beforeLoanFlags);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                // Cash inflow of 50 with matching bookkeeping: vault balance
                // +50, assets available +50, assets total unchanged so the
                // conservation identity (Δ AssetsTotal − Δ AssetsAvailable
                // − Δ ownedToVault == 0) holds.
                if (!BEAST_EXPECT(kAdjust(
                        ac.view(),
                        vaultKeylet,
                        Adjustments{
                            .assetsAvailable = 50,
                            .vaultAssets = 50,
                            .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                    continue;

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                // Principal and payments-remaining strictly decrease so the
                // non-full-repayment checks pass; total value drops by the
                // same 50 to keep interest-due nonnegative.
                sleLoan->at(sfPrincipalOutstanding) = Number(50);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                sleLoan->setFieldU32(sfNextPaymentDueDate, 1010);
                // Clear the impaired flag if it was set - a successful pay
                // implicitly unimpairs. Setting flags to 0 in either case
                // is consistent (unimpair preserves 0->0).
                sleLoan->setFieldU32(sfFlags, 0);
                ac.view().update(sleLoan);

                // Broker's DebtTotal falls by 50 to match Δ ownedToVault.
                {
                    auto sleBroker = ac.view().peek(brokerKeylet);
                    if (!BEAST_EXPECT(sleBroker))
                        continue;
                    sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(50);
                    ac.view().update(sleBroker);
                }

                // Deliberately mis-set LossUnrealized: the magnitude check
                // is the only one left to trip.
                {
                    auto sleVault = ac.view().peek(vaultKeylet);
                    if (!BEAST_EXPECT(sleVault))
                        continue;
                    sleVault->at(sfLossUnrealized) = c.afterLossUnrealized;
                    ac.view().update(sleVault);
                }

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(
                    "loan pay must decrease loss unrealized by the "
                    "pre-transaction amount the loan owed to the vault when "
                    "impaired, or leave it unchanged otherwise"));
            }
        }

        // ttLOAN_PAY: the broker's sfDebtTotal must fall by exactly the
        // amount the loan's ownedToVault fell. Every other loan-pay identity
        // is lined up so only the DebtTotal-tracking check trips. The broker
        // is touched so the "modify exactly the loan's broker" cardinality
        // guard passes.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Base DebtTotal mirrors the loan's ownedToVault (150).
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(150);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 2);
                sleLoan->setFieldU32(sfPaymentInterval, 10);
                sleLoan->setFieldU32(sfNextPaymentDueDate, 1000);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                return;

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(50);
            sleLoan->at(sfTotalValueOutstanding) = Number(100);
            sleLoan->setFieldU32(sfPaymentRemaining, 1);
            sleLoan->setFieldU32(sfNextPaymentDueDate, 1010);
            ac.view().update(sleLoan);

            // Broker DebtTotal drops by 30 (should drop by 50).
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(30);
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan pay broker debt total must track the change in the "
                "amount the loan owes to the vault"));
        }

        // Non-transferable vault shares (tfVaultShareNonTransferable at
        // creation, so the share issuance carries no lsfMPTCanTransfer bit)
        // may only be issued or burned by a Vault deposit / withdraw /
        // clawback / create / delete flow. Any other transaction touching
        // one of the holder MPToken positions of that issuance must trip
        // the check.
        {
            Keylet mptokenKeylet = keylet::amendments();
            Keylet vaultKeylet = keylet::amendments();
            Preclose const createNonTransferableVault =
                [&mptokenKeylet, &vaultKeylet, this](
                    Account const& a1, Account const& a2, Env& env) -> bool {
                Vault const vault{env};
                auto [tx, vk] = vault.create(
                    {.owner = a1, .asset = xrpIssue(), .flags = tfVaultShareNonTransferable});
                vaultKeylet = vk;
                env(tx);
                env.close();
                if (!BEAST_EXPECT(env.le(vaultKeylet)))
                    return false;
                // a2 deposits so a share MPToken position exists for
                // that holder in the base view.
                env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(100)}));
                env.close();

                auto const sleVault = env.le(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return false;
                mptokenKeylet = keylet::mptoken(sleVault->at(sfShareMPTID), a2.id());
                return BEAST_EXPECT(env.le(mptokenKeylet));
            };

            // Touch the share MPToken under a ttVAULT_SET (MustModifyVault
            // txn not excluded from the non-transferable check). The vault
            // itself is also touched so afterVault_ is populated; the
            // finalize walk sees touchedShareIssuances_ populated and the
            // share issuance carries no lsfMPTCanTransfer, so the check fires.
            doInvariantCheck(
                {"non-transferable vault shares must not move outside of "
                 "deposit, withdraw, or clawback"},
                [&mptokenKeylet, &vaultKeylet](Account const&, Account const&, ApplyContext& ac) {
                    auto sleMpt = ac.view().peek(mptokenKeylet);
                    if (!sleMpt)
                        return false;
                    ac.view().update(sleMpt);
                    auto sleVault = ac.view().peek(vaultKeylet);
                    if (!sleVault)
                        return false;
                    ac.view().update(sleVault);
                    return true;
                },
                XRPAmount{},
                STTx{ttVAULT_SET, [](STObject&) {}},
                // ValidVault::finalize skips all checks when the incoming
                // result is not tesSUCCESS, so the second pass passes and the
                // result stays tec.
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createNonTransferableVault);
        }

        // ttLOAN_MANAGE (default): the first-loss capital the vault receives
        // comes out of the loan-broker pseudo-account, so the two balances
        // must move by exactly opposite amounts. Credit the vault by 50 while
        // leaving the broker pseudo-account untouched: the residual is 50,
        // not zero, and the cover-conservation check must fire. The setup is
        // bespoke to give the invariant a real broker for the loan lookup.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault balance and assetsAvailable both +50 (as if first-loss
            // capital were returned), sourced from a2 rather than the broker
            // pseudo-account. The broker balance stays put, so the two
            // deltas do not cancel.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                return;

            // Modify the loan (before/after) so exactlyOneLoan passes.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(0);
            sleLoan->at(sfTotalValueOutstanding) = Number(0);
            sleLoan->setFieldU32(sfPaymentRemaining, 0);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must move the first-loss capital from the loan "
                "broker to the vault"));
        }

        // ttLOAN_MANAGE (default): loss unrealized must move by exactly
        // -beforeLoan.ownedToVault when the loan was impaired, or stay
        // unchanged when it was not. Base loan is not impaired, so the
        // expected delta is zero, yet the apply view drops loss unrealized
        // by 5, so the magnitude check trips. Other default-side identities
        // (cover, DebtTotal, vault-side conservation, cardinality) are all
        // lined up.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            // Prime the broker's cover so it can release first-loss capital
            // in the apply view.
            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Seed the base view with a non-impaired loan carrying a fully
            // funded principal, and a nonzero LossUnrealized on the vault so
            // a decrease in the apply view is a bounded delta.
            {
                auto const sleVaultRead = ov.read(vaultKeylet);
                if (!BEAST_EXPECT(sleVaultRead))
                    return;
                auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                sleVault->at(sfLossUnrealized) = Number(5);
                ov.rawReplace(sleVault);
            }
            // Seed the broker with a DebtTotal that mirrors the loan's
            // ownedToVault so the apply-view -100 lands at zero rather than
            // going negative (which would trip a separate ValidLoanBroker
            // check).
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault gets 100 first-loss capital: balance +100 and available
            // +100. Assets total is unchanged because the +100 inflow is
            // netted against the -100 write-off, so
            // `Δ AssetsTotal − Δ AssetsAvailable − Δ ownedToVault
            //   == 0 − 100 − (−100) == 0`.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{.assetsAvailable = 100, .vaultAssets = 100})))
                return;

            // Broker pseudo-account -100 mirrors the vault gain (cover
            // conservation).
            {
                auto slePseudo = ac.view().peek(keylet::account(brokerPseudoId));
                if (!BEAST_EXPECT(slePseudo))
                    return;
                slePseudo->at(sfBalance) = *slePseudo->at(sfBalance) - 100;
                ac.view().update(slePseudo);
            }

            // Broker: sfCoverAvailable drops by 100 (matches vault +100),
            // sfDebtTotal drops by 100 (matches Δ ownedToVault = -100).
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(100);
                ac.view().update(sleBroker);
            }

            // Zero the loan and default it.
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            // Drop LossUnrealized from 5 to 0. Loan was not impaired, so
            // expected delta is 0, actual delta is -5 -> magnitude trips.
            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                sleVault->at(sfLossUnrealized) = Number(0);
                ac.view().update(sleVault);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must decrease loss unrealized by the "
                "pre-transaction amount the loan owed to the vault when "
                "impaired, or leave it unchanged otherwise"));
        }

        // ttLOAN_MANAGE (default): mirror of the previous test for the
        // impaired branch. The base loan carries lsfLoanImpaired with
        // ownedToVault = 100, so the paper loss (LossUnrealized = 100) must
        // fall by exactly 100 on default. The apply view only drops it by
        // 50, so the residual is 50 and the magnitude check trips.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Base LossUnrealized == 100 (the impaired loan's owedToVault),
            // matching the state a real impair would produce.
            {
                auto const sleVaultRead = ov.read(vaultKeylet);
                if (!BEAST_EXPECT(sleVaultRead))
                    return;
                auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                sleVault->at(sfLossUnrealized) = Number(100);
                ov.rawReplace(sleVault);
            }
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                sleLoan->setFieldU32(sfFlags, lsfLoanImpaired);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{.assetsAvailable = 100, .vaultAssets = 100})))
                return;

            {
                auto slePseudo = ac.view().peek(keylet::account(brokerPseudoId));
                if (!BEAST_EXPECT(slePseudo))
                    return;
                slePseudo->at(sfBalance) = *slePseudo->at(sfBalance) - 100;
                ac.view().update(slePseudo);
            }
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(100);
                ac.view().update(sleBroker);
            }
            // Zero the loan and default it (clear the impaired flag as the
            // realized loss is now on the ledger).
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            // Drop LossUnrealized from 100 to 50 (should drop to 0).
            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                sleVault->at(sfLossUnrealized) = Number(50);
                ac.view().update(sleVault);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must decrease loss unrealized by the "
                "pre-transaction amount the loan owed to the vault when "
                "impaired, or leave it unchanged otherwise"));
        }

        // ttLOAN_MANAGE (default): under featureLendingProtocolV1_1 the
        // invariant expects the loan's broker to have been touched so both
        // before/after snapshots are captured. A defaulted loan that leaves
        // the broker SLE untouched (a broken transactor) must be flagged.
        // Other default-side identities fire alongside because the setup is
        // deliberately minimal, but the target message is what we assert on.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Zero the loan and mark it defaulted so the flag-transition and
            // paid-off checks pass and the broker-cardinality check is
            // reached.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(0);
            sleLoan->at(sfTotalValueOutstanding) = Number(0);
            sleLoan->setFieldU32(sfPaymentRemaining, 0);
            sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
            ac.view().update(sleLoan);

            // The vault must be touched so before/afterVault_ are captured and
            // finalize dispatches to finalizeLoanManage at all.
            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                ac.view().update(sleVault);
            }

            // Deliberately do not touch the broker SLE. before/afterBroker_
            // both stay empty, tripping the cardinality guard.

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must modify exactly the loan's broker"));
        }

        // ttLOAN_MANAGE (default): if the broker released first-loss capital
        // (its sfCoverAvailable moved) the vault balance ledger entry must
        // reflect it. Broker's cover changes in the apply view but neither
        // the vault's pseudo-account balance nor its accounting fields do,
        // so deltaAssets(pseudoId) is nullopt and the "must change vault
        // balance when first-loss capital is returned" guard trips.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Seed a non-impaired defaulting loan and a matching DebtTotal
            // on the broker.
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Broker cover drops by 100 (as if capital were released) but
            // the vault pseudo-account balance is not touched -
            // maybeVaultDeltaAssets is nullopt and the guard fires.
            auto sleBroker = ac.view().peek(brokerKeylet);
            if (!BEAST_EXPECT(sleBroker))
                return;
            sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
            sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(100);
            ac.view().update(sleBroker);

            // Zero the loan and default it so paid-off and flag-transition
            // checks pass.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(0);
            sleLoan->at(sfTotalValueOutstanding) = Number(0);
            sleLoan->setFieldU32(sfPaymentRemaining, 0);
            sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
            ac.view().update(sleLoan);

            // Touch the vault SLE (without moving its pseudo-account balance)
            // so before/afterVault_ are captured and finalize dispatches to
            // finalizeLoanManage.
            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                ac.view().update(sleVault);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must change vault balance when first-loss "
                "capital is returned"));
        }

        // ttLOAN_MANAGE (default): sfAssetsAvailable must grow by exactly
        // the amount the broker released (DefaultCovered = Δ broker
        // sfCoverAvailable). Broker releases 100 and the vault balance
        // grows by 100, but assets available is only credited with 50, so
        // the identity fires. The "adds-up" check fires alongside because
        // vault balance and assets available diverge; the assertion is
        // scoped to our target message.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault: pseudo-account balance +100 (real cash inflow), but
            // AssetsAvailable only +50. AssetsTotal is unchanged so the
            // vault-side conservation identity would want
            // Δ ownedToVault == Δ AssetsTotal − Δ AssetsAvailable == -50.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{.assetsAvailable = 50, .vaultAssets = 100})))
                return;

            // Broker pseudo-account -100 mirrors the vault gain (cover
            // conservation intact).
            {
                auto slePseudo = ac.view().peek(keylet::account(brokerPseudoId));
                if (!BEAST_EXPECT(slePseudo))
                    return;
                slePseudo->at(sfBalance) = *slePseudo->at(sfBalance) - 100;
                ac.view().update(slePseudo);
            }
            // Broker: cover -100 mirrors the pseudo-account balance (the
            // sfCoverAvailable == pseudoBalance invariant stays intact).
            // sfDebtTotal drops by 100 to match Δ ownedToVault.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(100);
                ac.view().update(sleBroker);
            }
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must increase assets available by the default "
                "covered amount"));
        }

        // ttLOAN_MANAGE (default): the broker's sfDebtTotal must fall by
        // exactly the amount the loan owed to the vault. Every other
        // default-side identity (cover conservation, adds-up, DefaultCovered
        // == Δ AssetsAvailable, vault-side conservation) balances; only
        // sfDebtTotal is deliberately mis-adjusted (drops by 50 instead of
        // 100), so the DebtTotal-tracking check trips in isolation.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault gets 100 first-loss capital: balance +100, available
            // +100, total unchanged (write-off nets the inflow).
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{.assetsAvailable = 100, .vaultAssets = 100})))
                return;

            // Broker pseudo-account -100 (cover conservation).
            {
                auto slePseudo = ac.view().peek(keylet::account(brokerPseudoId));
                if (!BEAST_EXPECT(slePseudo))
                    return;
                slePseudo->at(sfBalance) = *slePseudo->at(sfBalance) - 100;
                ac.view().update(slePseudo);
            }
            // Broker: cover -100 (matches Δ AssetsAvailable), but
            // sfDebtTotal only drops by 50, not the expected 100.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(50);
                ac.view().update(sleBroker);
            }
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default broker debt total must track the change in the "
                "amount the loan owes to the vault"));
        }

        // ttLOAN_MANAGE (default): the invariant reads the broker through the
        // defaulted loan. If the loan carries a stale or zero LoanBrokerID
        // the lookup fails, so the check that guards the cover-conservation
        // step must report it explicitly rather than silently skip.
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            // Pre-insert a loan carrying a default (zero) sfLoanBrokerID so
            // the invariant's broker lookup returns nullopt.
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Touch the vault so ValidVault::finalize enters
            // finalizeLoanManage; the tfLoanDefault path is what carries the
            // "loan default loan broker must exist" check we are asserting.
            auto sleVault = ac.view().peek(vaultKeylet);
            if (!BEAST_EXPECT(sleVault))
                return;
            ac.view().update(sleVault);

            // Modify the loan (before/after) so exactlyOneLoan passes and the
            // broker lookup is actually reached.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(0);
            sleLoan->at(sfTotalValueOutstanding) = Number(0);
            sleLoan->setFieldU32(sfPaymentRemaining, 0);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains("loan default loan broker must exist"));
        }

        // A loan may only be deleted by a LoanDelete transaction, and only once
        // it is fully paid off. Both branches are exercised by creating a real
        // loan in the Preclose (so it exists in the base ledger with outstanding
        // principal) and then erasing it in the Precheck.
        {
            Keylet loanKeylet = keylet::amendments();
            auto const precloseLoan = [&loanKeylet, this](
                                          Account const& a1, Account const& a2, Env& env) -> bool {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

                Vault const vault{env};
                auto [tx, vaultKeylet] = vault.create({.owner = a1, .asset = xrpAsset});
                env(tx);
                env.close();
                if (!BEAST_EXPECT(env.le(vaultKeylet)))
                    return false;

                env(vault.deposit(
                    {.depositor = a1, .id = vaultKeylet.key, .amount = xrpAsset(100)}));
                env.close();

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(env.seq(a1)));
                env(loan_broker::set(a1, vaultKeylet.key), Fee(kIncrement));
                env.close();
                auto const brokerSle = env.le(brokerKeylet);
                if (!BEAST_EXPECT(brokerSle))
                    return false;

                loanKeylet = keylet::loan(
                    brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
                env(loan::set(a2, brokerKeylet.key, xrpAsset(50).value()),
                    Sig(sfCounterpartySignature, a1),
                    Fee(env.current()->fees().base * 2));
                env.close();
                return BEAST_EXPECT(env.le(loanKeylet));
            };

            auto const eraseLoan = [&loanKeylet](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(loanKeylet);
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            };

            // Deleting the loan under any transaction type other than LoanDelete
            // (here the neutral ttACCOUNT_SET) is a violation, even while the
            // loan still has outstanding obligations: the transaction-type check
            // fires before the not-fully-paid-off check.
            doInvariantCheck(
                {"Loan deleted by a transaction other than LoanDelete"},
                eraseLoan,
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLoan);

            // Deleting the loan via LoanDelete while it still has outstanding
            // obligations is a violation: the transaction-type check passes and
            // the not-fully-paid-off check fires.
            doInvariantCheck(
                {"Loan deleted while not fully paid off"},
                eraseLoan,
                XRPAmount{},
                STTx{ttLOAN_DELETE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLoan);
        }

        // The not-fully-paid-off check inspects four fields disjunctively:
        // sfPaymentRemaining, sfTotalValueOutstanding, sfPrincipalOutstanding
        // and sfManagementFeeOutstanding. The block above covers the composite
        // case (all non-zero). Below, exercise each balance field on its own
        // with sfPaymentRemaining pinned to zero, so the check fires solely
        // on that disjunct. The base-view loan is seeded bespoke so its
        // fields can be shaped precisely.
        for (auto const field : {
                 &sfTotalValueOutstanding,
                 &sfPrincipalOutstanding,
                 &sfManagementFeeOutstanding,
             })
        {
            Env env{*this, defaultAmendments()};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
            // Seed a loan whose only outstanding obligation is on `field`,
            // with sfPaymentRemaining already at zero so the paid-off
            // disjunct being tested is the balance field alone.
            {
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(*field) = Number(10);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{ttLOAN_DELETE, [](STObject&) {}};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                continue;
            ac.view().erase(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                continue;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains("Loan deleted while not fully paid off"));
        }

        {
            Keylet loanKeylet = keylet::amendments();
            auto const precloseLoan = [&loanKeylet, this](
                                          Account const& a1, Account const& a2, Env& env) -> bool {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

                Vault const vault{env};
                auto [tx, vaultKeylet] = vault.create({.owner = a1, .asset = xrpAsset});
                env(tx);
                env.close();
                if (!BEAST_EXPECT(env.le(vaultKeylet)))
                    return false;

                env(vault.deposit(
                    {.depositor = a1, .id = vaultKeylet.key, .amount = xrpAsset(100)}));
                env.close();

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(env.seq(a1)));
                env(loan_broker::set(a1, vaultKeylet.key), Fee(kIncrement));
                env.close();
                auto const brokerSle = env.le(brokerKeylet);
                if (!BEAST_EXPECT(brokerSle))
                    return false;

                loanKeylet = keylet::loan(
                    brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
                env(loan::set(a2, brokerKeylet.key, xrpAsset(50).value()),
                    Sig(sfCounterpartySignature, a1),
                    Fee(env.current()->fees().base * 2));
                env.close();
                return BEAST_EXPECT(env.le(loanKeylet));
            };

            // (XLS-66 §3.1.5 precondition 1): a LoanBrokerDelete
            // transaction must not touch any loan. Broker delete requires
            // OwnerCount == 0 (no loans reference the broker); touching a
            // loan alongside the delete points at either an
            // OwnerCount-tracking bug or a spurious cascading write.
            doInvariantCheck(
                {"LoanBrokerDelete must not touch any loan"},
                [&loanKeylet](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(loanKeylet);
                    if (!sle)
                        return false;
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_DELETE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                precloseLoan);
        }

        // Loan interest due (total value less principal and management fee)
        // must never be negative. A neutral transaction type is used so the
        // vault invariants short-circuit and only the loan check fires. The
        // loan object is created directly with principal 100, total value 90
        // and management fee 0, so interest due = 90 - 100 - 0 = -10 (< 0)
        // while every individual field stays non-negative.
        doInvariantCheck(
            {"Loan interest due is negative"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const vaultKeylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(90);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ac.view().insert(sleLoan);
                return true;
            });

        // Each of these loan STNumber fields must never be negative. A neutral
        // transaction type is used so the vault invariants short-circuit and
        // only the loan check fires. The loan is created directly with a single
        // field set negative while the paid-off bookkeeping is kept consistent
        // so that only the "<field> is negative" check trips.
        for (auto const field : {
                 &sfLoanServiceFee,
                 &sfLatePaymentFee,
                 &sfClosePaymentFee,
                 &sfPrincipalOutstanding,
                 &sfTotalValueOutstanding,
                 &sfManagementFeeOutstanding,
             })
        {
            // The outstanding-balance fields also feed the paid-off checks, so
            // a loan carrying one must still have payments remaining; a loan
            // with only a negative fee stays fully paid off (zero remaining).
            bool const isOutstanding = *field == sfPrincipalOutstanding ||
                *field == sfTotalValueOutstanding || *field == sfManagementFeeOutstanding;
            doInvariantCheck(
                {field->getName() + " is negative"},
                [&, field](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const vaultKeylet =
                        keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                    auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfPrincipalOutstanding) = Number(0);
                    sleLoan->at(sfTotalValueOutstanding) = Number(0);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->at(*field) = Number(-10);
                    sleLoan->setFieldU32(sfPaymentRemaining, isOutstanding ? 1 : 0);
                    ac.view().insert(sleLoan);
                    return true;
                });
        }

        // Mirror of the loop above for the strictly-positive constraint: a
        // loan's sfPeriodicPayment must always be > 0. Cover both boundary
        // failure modes (zero and negative).
        for (Number const& badValue : {Number(0), Number(-1)})
        {
            doInvariantCheck(
                {std::string{sfPeriodicPayment.getName()} + " is zero or negative"},
                [&, badValue](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const vaultKeylet =
                        keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                    auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfPrincipalOutstanding) = Number(0);
                    sleLoan->at(sfTotalValueOutstanding) = Number(0);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = badValue;
                    sleLoan->setFieldU32(sfPaymentRemaining, 0);
                    ac.view().insert(sleLoan);
                    return true;
                });
        }

        // A loan with sfPaymentRemaining == 0 must be fully paid off in every
        // outstanding-balance dimension. Insert a bare loan that reports zero
        // payments remaining but still carries a non-zero principal owed; the
        // paid-off invariant must reject it before the later broker-existence
        // check has a chance to run.
        doInvariantCheck(
            {"Loan with zero payments remaining has not been paid off"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const vaultKeylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                ac.view().insert(sleLoan);
                return true;
            });

        // Converse: a loan whose outstanding balances are all zero has been
        // fully paid off and must carry zero payments remaining. Insert a
        // fully-zeroed loan with sfPaymentRemaining = 1 to trip the check.
        doInvariantCheck(
            {"Fully paid off Loan still has payments remaining"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const vaultKeylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ac.view().insert(sleLoan);
                return true;
            });

        // A loan must reference a live loan broker. A bare loan SLE is
        // inserted with every other loan-level field kept consistent so the
        // earlier ValidLoan checks pass; sfLoanBrokerID defaults to zero,
        // which resolves to no broker, and the broker-existence check trips.
        doInvariantCheck(
            {"Loan broker does not exist"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const vaultKeylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto const loanKeylet = keylet::loan(vaultKeylet.key, SeqProxy::rawSequence(1));
                auto sleLoan = std::make_shared<SLE>(loanKeylet);
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                ac.view().insert(sleLoan);
                return true;
            });

        // A loan's broker must in turn reference a live vault. A real broker
        // is created in the preclose so its sfVaultID points at an existing
        // vault; the precheck then erases that vault and inserts a loan
        // referencing the broker, so the broker-existence check passes and
        // the broker-vault-existence check trips.
        {
            Keylet brokerKeylet = keylet::amendments();
            auto const precloseBroker = [&brokerKeylet, this](
                                            Account const& a1, Account const&, Env& env) -> bool {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
                brokerKeylet = this->createLoanBroker(a1, env, xrpAsset);
                env.close();
                return BEAST_EXPECT(env.le(brokerKeylet));
            };

            doInvariantCheck(
                {"Loan broker vault does not exist"},
                [&brokerKeylet](Account const&, Account const&, ApplyContext& ac) {
                    auto sleBroker = ac.view().peek(brokerKeylet);
                    if (!sleBroker)
                        return false;
                    auto sleVault = ac.view().peek(keylet::vault(sleBroker->at(sfVaultID)));
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);

                    auto const loanKeylet =
                        keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                    sleLoan->at(sfPrincipalOutstanding) = Number(0);
                    sleLoan->at(sfTotalValueOutstanding) = Number(0);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 0);
                    ac.view().insert(sleLoan);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseBroker);
        }

        // ttVAULT_SET: owner is immutable (enforced by
        // NoModifiedUnmodifiableFields under featureLendingProtocolV1_1).
        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setAccountID(sfOwner, a2.id());
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttVAULT_SET: withdrawal policy is immutable
        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldU8(
                    sfWithdrawalPolicy,
                    static_cast<std::uint8_t>(sleVault->getFieldU8(sfWithdrawalPolicy) + 1));
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttVAULT_SET: scale is immutable
        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldU8(
                    sfScale, static_cast<std::uint8_t>(sleVault->getFieldU8(sfScale) + 1));
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        testcase << "Vault create";
        doInvariantCheck(
            {
                "created vault must be empty",
                "updated zero sized vault must have no assets outstanding",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsTotal] = 9;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "created vault must be empty",
                "updated zero sized vault must have no assets available",
                "assets available must not be greater than assets outstanding",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsAvailable] = 9;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "created vault must be empty",
                "loss unrealized must not exceed the difference between assets "
                "outstanding and available",
                "vault transaction must not change loss unrealized",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfLossUnrealized] = 1;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "created vault must be empty",
                "create operation must not have updated a vault",
                "invalid OutstandingAmount balance 0 9 0",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().update(sleVault);
                (*sleShares)[sfOutstandingAmount] = 9;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "assets maximum must not be negative",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsMaximum] = Number(-1);
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"create operation must not have updated a vault",
             "shares issuer and vault pseudo-account must be the same",
             "shares issuer must be a pseudo-account",
             "shares issuer pseudo-account must point back to the vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().update(sleVault);
                (*sleShares)[sfIssuer] = a1.id();
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault created by a wrong transaction type", "account root created illegally"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // The code below will create a valid vault with (almost) all
                // the invariants holding. Except one: it is created by the
                // wrong transaction type.
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(sequence));
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto pseudoId = pseudoAccountAddress(ac.view(), vaultKeylet.key);
                // Create pseudo-account.
                auto sleAccount = std::make_shared<SLE>(keylet::account(pseudoId));
                sleAccount->setAccountID(sfAccount, pseudoId);
                sleAccount->setFieldAmount(sfBalance, STAmount{});
                std::uint32_t const seqno =                             //
                    ac.view().rules().enabled(featureSingleAssetVault)  //
                    ? 0                                                 //
                    : sequence;
                sleAccount->setFieldU32(sfSequence, seqno);
                sleAccount->setFieldU32(
                    sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
                ac.view().insert(sleAccount);

                auto const sharesMptId = makeMptID(sequence, pseudoId);
                auto const sharesKeylet = keylet::mptokenIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(pseudoId), sharesKeylet, describeOwnerDir(pseudoId));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                sleShares->at(sfIssuer) = pseudoId;
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                sleVault->at(sfAccount) = pseudoId;
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = a1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;

                ac.view().insert(sleVault);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        doInvariantCheck(
            {"shares issuer and vault pseudo-account must be the same",
             "shares issuer pseudo-account must point back to the vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(sequence));
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto pseudoId = pseudoAccountAddress(ac.view(), vaultKeylet.key);
                // Create pseudo-account.
                auto sleAccount = std::make_shared<SLE>(keylet::account(pseudoId));
                sleAccount->setAccountID(sfAccount, pseudoId);
                sleAccount->setFieldAmount(sfBalance, STAmount{});
                std::uint32_t const seqno =                             //
                    ac.view().rules().enabled(featureSingleAssetVault)  //
                    ? 0                                                 //
                    : sequence;
                sleAccount->setFieldU32(sfSequence, seqno);
                sleAccount->setFieldU32(
                    sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                // sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
                // Setting wrong vault key
                sleAccount->setFieldH256(sfVaultID, uint256(42));
                ac.view().insert(sleAccount);

                auto const sharesMptId = makeMptID(sequence, pseudoId);
                auto const sharesKeylet = keylet::mptokenIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(pseudoId), sharesKeylet, describeOwnerDir(pseudoId));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                sleShares->at(sfIssuer) = pseudoId;
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                // sleVault->at(sfAccount) = pseudoId;
                // Setting wrong pseudo account ID
                sleVault->at(sfAccount) = a2.id();
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = a1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;

                ac.view().insert(sleVault);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        doInvariantCheck(
            {"shares issuer and vault pseudo-account must be the same", "shares issuer must exist"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(sequence));
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto const sharesMptId = makeMptID(sequence, a2.id());
                auto const sharesKeylet = keylet::mptokenIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(a2.id()), sharesKeylet, describeOwnerDir(a2.id()));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                // Setting wrong pseudo account ID
                sleShares->at(sfIssuer) = AccountID(42);
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                sleVault->at(sfAccount) = a2.id();
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = a1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;

                ac.view().insert(sleVault);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        testcase << "Vault deposit";
        doInvariantCheck(
            {"deposit must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [](Adjustments& sample) {
                                   sample.vaultAssets.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"deposit assets outstanding must not exceed assets maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 200, [&](Adjustments& sample) {
                                   sample.assetsMaximum = 1;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // This really convoluted unit tests makes the zero balance on the
        // depositor, by sending them the same amount as the transaction fee.
        // The operation makes no sense, but the defensive check in
        // ValidVault::finalize is otherwise impossible to trigger.
        doInvariantCheck(
            {"deposit must increase vault balance", "deposit must change depositor balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(a4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return kAdjust(ac.view(), keylet, kArgs(a3.id(), -10, [&](Adjustments& sample) {
                                   sample.accountAssets->amount = -100;
                               }));
            },
            XRPAmount{100},
            STTx{
                ttVAULT_DEPOSIT,
                [&](STObject& tx) {
                    tx[sfFee] = XRPAmount(100);
                    tx[sfAccount] = a3.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"deposit must increase vault balance",
             "deposit must decrease depositor balance",
             "deposit must change vault and depositor balance by equal amount",
             "deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops from A2 to A3 to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 10;
                ac.view().update(sleA3);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.vaultAssets = -20;
                                   sample.accountAssets->amount = 10;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change depositor balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops from A3 to vault to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 10;
                ac.view().update(sleA3);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.accountAssets->amount = 0;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change depositor shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.accountShares.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change vault shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments& sample) {
                                   sample.sharesTotal = 0;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must increase depositor shares",
             "deposit must change depositor and vault shares by equal amount",
             "deposit must not change vault balance by more than deposited "
             "amount"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = -5;
                                   sample.sharesTotal = -10;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(5); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit and assets outstanding must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 2000;
                ac.view().update(sleA3);

                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.assetsTotal = 11;
                               }));
            },
            XRPAmount{2000},
            STTx{
                ttVAULT_DEPOSIT,
                [&](STObject& tx) {
                    tx[sfAmount] = XRPAmount(10);
                    tx[sfDelegate] = a3.id();
                    tx[sfFee] = XRPAmount(2000);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.assetsTotal = 7;
                                   sample.assetsAvailable = 7;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        testcase << "Vault withdrawal";
        doInvariantCheck(
            {"withdrawal must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [](Adjustments& sample) {
                                   sample.vaultAssets.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // Almost identical to the really convoluted test for deposit, where the
        // depositor spends only the transaction fee. In case of withdrawal,
        // this test is almost the same as normal withdrawal where the
        // sfDestination would have been A4, but has been omitted.
        doInvariantCheck(
            {"withdrawal must change one destination balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(a4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return kAdjust(ac.view(), keylet, kArgs(a3.id(), -10, [&](Adjustments& sample) {
                                   sample.accountAssets->amount = -100;
                               }));
            },
            XRPAmount{100},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) {
                    tx[sfFee] = XRPAmount(100);
                    tx[sfAccount] = a3.id();
                    // This commented out line causes the invariant violation.
                    // tx[sfDestination] = A4.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {
                "withdrawal must change vault and destination balance by equal amount",
                "withdrawal must decrease vault balance",
                "withdrawal must increase destination balance",
                "withdrawal and assets outstanding must add up",
                "withdrawal and assets available must add up",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops from A2 to A3 to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 10;
                ac.view().update(sleA3);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.vaultAssets = 10;
                                   sample.accountAssets->amount = -20;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change one destination balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                if (!kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                 *sample.vaultAssets -= 5;
                             })))
                    return false;
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 5;
                ac.view().update(sleA3);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [&](STObject& tx) { tx.setAccountID(sfDestination, a3.id()); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change depositor shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change vault shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [](Adjustments& sample) {
                                   sample.sharesTotal = 0;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must decrease depositor shares",
             "withdrawal must change depositor and vault shares by equal "
             "amount"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = 5;
                                   sample.sharesTotal = 10;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal and assets outstanding must add up",
             "withdrawal and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.assetsTotal = -15;
                                   sample.assetsAvailable = -15;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal and assets outstanding must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 2000;
                ac.view().update(sleA3);

                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.assetsTotal = -7;
                               }));
            },
            XRPAmount{2000},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) {
                    tx[sfAmount] = XRPAmount(10);
                    tx[sfDelegate] = a3.id();
                    tx[sfFee] = XRPAmount(2000);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        auto const precloseMpt = [&](Account const& a1, Account const& a2, Env& env) -> bool {
            env.fund(XRP(1000), a3, a4);

            // Create MPT asset
            {
                json::Value jv;
                jv[sfAccount] = a3.human();
                jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
                jv[sfFlags] = tfMPTCanTransfer;
                env(jv);
                env.close();
            }

            auto const mptID = makeMptID(env.seq(a3) - 1, a3);
            Asset const asset = MPTIssue(mptID);
            // Authorize A1 A2 A4
            {
                json::Value jv;
                jv[sfAccount] = a1.human();
                jv[sfTransactionType] = jss::MPTokenAuthorize;
                jv[sfMPTokenIssuanceID] = to_string(mptID);
                env(jv);
                jv[sfAccount] = a2.human();
                env(jv);
                jv[sfAccount] = a4.human();
                env(jv);

                env.close();
            }
            // Send tokens to A1 A2 A4
            {
                env(pay(a3, a1, asset(1000)));
                env(pay(a3, a2, asset(1000)));
                env(pay(a3, a4, asset(1000)));
                env.close();
            }

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = a1, .asset = asset});
            env(tx);
            env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = asset(10)}));
            env(vault.deposit({.depositor = a2, .id = keylet.key, .amount = asset(10)}));
            env(vault.deposit({.depositor = a4, .id = keylet.key, .amount = asset(10)}));
            return true;
        };

        doInvariantCheck(
            {"withdrawal must decrease depositor shares",
             "withdrawal must change depositor and vault shares by equal "
             "amount"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = 5;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [&](STObject& tx) { tx[sfAccount] = a3.id(); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt,
            TxAccount::A2);

        testcase << "Vault clawback";
        doInvariantCheck(
            {"clawback must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -1, [&](Adjustments& sample) {
                                   sample.vaultAssets.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [&](STObject& tx) { tx[sfAccount] = a3.id(); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt);

        // Not the same as below check: attempt to clawback XRP
        doInvariantCheck(
            {"clawback may only be performed by the asset issuer"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // Not the same as above check: attempt to clawback MPT by bad account
        doInvariantCheck(
            {"clawback may only be performed by the asset issuer"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [&](STObject& tx) { tx[sfAccount] = a4.id(); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must decrease vault balance",
             "clawback must decrease holder shares",
             "clawback must change vault shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a4.id(), 10, [&](Adjustments& sample) {
                                   sample.sharesTotal = 0;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = a3.id();
                    tx[sfHolder] = a4.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must change holder shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a4.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares.reset();
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = a3.id();
                    tx[sfHolder] = a4.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must change holder and vault shares by equal amount",
             "clawback and assets outstanding must add up",
             "clawback and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a4.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = -8;
                                   sample.assetsTotal = -7;
                                   sample.assetsAvailable = -7;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = a3.id();
                    tx[sfHolder] = a4.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt);

        // ─────────────────────────────────────────────────────────────
        // Closed-ended vault invariants added in ValidVault::finalize (create must supply both
        // dates and satisfy the redemption-buffer gap), deposit only in Subscription / NoPhase,
        // withdraw not in Investment, loan origination only in Investment.

        using d = NetClock::duration;
        using tp = NetClock::time_point;

        auto const closedEnded = std::to_underlying(VaultKind::ClosedEnded);

        // Vault keylet captured by precloseClosedEnded so precheck does not have to rederive it
        // from ac.view().seq(), which depends on how many env.close() calls preclose issued.
        Keylet closedEndedKeylet = keylet::amendments();

        // Preclose that creates a closed-ended vault (in Subscription), optionally seeds it with
        // three deposits (so a1/a2/a3 hold a share MPToken that kAdjust can then adjust), and
        // optionally advances parent close time past SubscriptionDate. A negative @p advanceBySub
        // leaves the vault in Subscription.
        auto const precloseClosedEnded = [&](std::int32_t advanceBySub, bool doDeposit) {
            return [&, advanceBySub, doDeposit](
                       Account const& a1, Account const& a2, Env& env) -> bool {
                env.fund(XRP(1000), a3, a4);
                auto const sub = env.now().time_since_epoch().count() + 60;
                auto const red = sub + kMinInvestmentPeriod + 1'000'000;
                Vault const vault{env};
                auto [tx, keylet] = vault.create(
                    {.owner = a1,
                     .asset = xrpIssue(),
                     .vaultKind = closedEnded,
                     .subscriptionDate = sub,
                     .redemptionDate = red});
                env(tx);
                closedEndedKeylet = keylet;
                if (doDeposit)
                {
                    env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
                    env(vault.deposit({.depositor = a2, .id = keylet.key, .amount = XRP(10)}));
                    env(vault.deposit({.depositor = a3, .id = keylet.key, .amount = XRP(10)}));
                }
                if (advanceBySub >= 0)
                    env.close(tp{d{sub + advanceBySub}});
                return true;
            };
        };

        // Manually insert a bare closed-ended vault (+ pseudo-account + share MPTokenIssuance)
        // directly into the view, bypassing the transactor path. Used to synthesize ttVAULT_CREATE
        // states no legitimate transactor would produce.
        auto const insertBareClosedEndedVault =
            [closedEnded](
                ApplyContext& ac,
                Account const& owner,
                std::optional<std::uint32_t> subscriptionDate,
                std::optional<std::uint32_t> redemptionDate) -> bool {
            auto const sequence = ac.view().seq();
            auto const vaultKeylet = keylet::vault(owner.id(), SeqProxy::rawSequence(sequence));
            auto sleVault = std::make_shared<SLE>(vaultKeylet);
            auto const vaultPage = ac.view().dirInsert(
                keylet::ownerDir(owner.id()), sleVault->key(), describeOwnerDir(owner.id()));
            if (!vaultPage)
                return false;
            sleVault->setFieldU64(sfOwnerNode, *vaultPage);

            auto const pseudoId = pseudoAccountAddress(ac.view(), vaultKeylet.key);
            auto sleAccount = std::make_shared<SLE>(keylet::account(pseudoId));
            sleAccount->setAccountID(sfAccount, pseudoId);
            sleAccount->setFieldAmount(sfBalance, STAmount{});
            sleAccount->setFieldU32(sfSequence, 0);
            sleAccount->setFieldU32(sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
            sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
            ac.view().insert(sleAccount);

            auto const sharesMptId = makeMptID(sequence, pseudoId);
            auto const sharesKeylet = keylet::mptokenIssuance(sharesMptId);
            auto sleShares = std::make_shared<SLE>(sharesKeylet);
            auto const sharesPage = ac.view().dirInsert(
                keylet::ownerDir(pseudoId), sharesKeylet, describeOwnerDir(pseudoId));
            if (!sharesPage)
                return false;
            sleShares->setFieldU64(sfOwnerNode, *sharesPage);
            sleShares->at(sfFlags) = 0;
            sleShares->at(sfIssuer) = pseudoId;
            sleShares->at(sfOutstandingAmount) = 0;
            sleShares->at(sfSequence) = sequence;

            sleVault->at(sfAccount) = pseudoId;
            sleVault->at(sfFlags) = 0;
            sleVault->at(sfSequence) = sequence;
            sleVault->at(sfOwner) = owner.id();
            sleVault->setFieldIssue(sfAsset, STIssue{sfAsset, Asset{xrpIssue()}});
            sleVault->at(sfAssetsTotal) = Number(0);
            sleVault->at(sfAssetsAvailable) = Number(0);
            sleVault->at(sfLossUnrealized) = Number(0);
            sleVault->at(sfShareMPTID) = sharesMptId;
            sleVault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;
            sleVault->at(sfVaultKind) = closedEnded;
            if (subscriptionDate)
                sleVault->at(sfSubscriptionDate) = *subscriptionDate;
            if (redemptionDate)
                sleVault->at(sfRedemptionDate) = *redemptionDate;

            ac.view().insert(sleVault);
            ac.view().insert(sleShares);
            return true;
        };

        testcase << "Vault create closed-ended";

        // A fresh closed-ended vault must carry both SubscriptionDate and RedemptionDate.
        doInvariantCheck(
            {"closed-ended vault must have SubscriptionDate and RedemptionDate"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return insertBareClosedEndedVault(ac, a1, std::nullopt, std::nullopt);
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        // Gap smaller than MIN_INVESTMENT_PERIOD but with RedemptionDate > SubscriptionDate;
        // exercises the sub-minimum branch of the gap check.
        doInvariantCheck(
            {"closed-ended vault RedemptionDate - SubscriptionDate must be "
             "within [MIN_INVESTMENT_PERIOD, MAX_INVESTMENT_PERIOD)"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                std::uint32_t const sub = 1'000'000'000;
                std::uint32_t const red = sub + kMinInvestmentPeriod - 1;
                return insertBareClosedEndedVault(ac, a1, sub, red);
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        // RedemptionDate strictly before SubscriptionDate; the signed int64 gap is negative and
        // is caught by the sub-minimum branch of the gap check.
        doInvariantCheck(
            {"closed-ended vault RedemptionDate - SubscriptionDate must be "
             "within [MIN_INVESTMENT_PERIOD, MAX_INVESTMENT_PERIOD)"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                std::uint32_t const sub = 1'000'000'000;
                std::uint32_t const red = sub - 1;
                return insertBareClosedEndedVault(ac, a1, sub, red);
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        // Gap exactly MAX_INVESTMENT_PERIOD is out of range (bound is half-open on the right).
        doInvariantCheck(
            {"closed-ended vault RedemptionDate - SubscriptionDate must be "
             "within [MIN_INVESTMENT_PERIOD, MAX_INVESTMENT_PERIOD)"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                std::uint32_t const sub = 1'000'000'000;
                std::uint32_t const red = sub + kMaxInvestmentPeriod;
                return insertBareClosedEndedVault(ac, a1, sub, red);
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        testcase << "Vault deposit closed-ended";

        // A deposit into a closed-ended vault that has advanced past SubscriptionDate. kArgs
        // simulates an otherwise valid deposit shape so only the phase invariant fires.
        doInvariantCheck(
            {"deposit only allowed in Subscription or NoPhase"},
            [&](Account const&, Account const& a2, ApplyContext& ac) {
                return kAdjust(
                    ac.view(), closedEndedKeylet, kArgs(a2.id(), 10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseClosedEnded(/*advanceBySub=*/1, /*doDeposit=*/true),
            TxAccount::A2);

        testcase << "Vault withdrawal closed-ended";

        // A withdrawal from a closed-ended vault in the Investment phase.
        doInvariantCheck(
            {"withdrawal not allowed during Investment phase"},
            [&](Account const&, Account const& a2, ApplyContext& ac) {
                return kAdjust(
                    ac.view(), closedEndedKeylet, kArgs(a2.id(), -10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseClosedEnded(/*advanceBySub=*/1, /*doDeposit=*/true),
            TxAccount::A2);

        testcase << "Vault loan set";

        // ttLOAN_SET against a closed-ended vault that is not in Investment. finalizeLoanSet fires
        // on any vault mutation; touching the vault SLE with no field change is sufficient.
        doInvariantCheck(
            {"loan origination only allowed in Investment phase"},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sleVault = ac.view().peek(closedEndedKeylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseClosedEnded(/*advanceBySub=*/-1, /*doDeposit=*/false));

        testcase << "Vault loan set - closed-ended final payment past "
                    "RedemptionDate";

        // A newly-created loan against a closed-ended vault must satisfy StartDate +
        // PaymentInterval * PaymentRemaining < RedemptionDate. LoanSet::preclaim enforces the same
        // bound; this test synthesises an invalid loan directly in the ApplyView so the invariant
        // catches it even when preclaim is bypassed.
        Keylet closedEndedBrokerKeylet = keylet::amendments();
        std::uint32_t closedEndedRed = 0;
        doInvariantCheck(
            {"closed-ended loan final payment must precede RedemptionDate"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                // Touch the vault so ValidVault::finalizeLoanSet sees an
                // entry in afterVault_; the vault is in Investment, so
                // finalizeLoanSet itself passes.
                auto sleVault = ac.view().peek(closedEndedKeylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);

                // Read the broker's next loan sequence to build the loan
                // keylet the same way LoanSet::doApply would.
                auto sleBroker = ac.view().peek(closedEndedBrokerKeylet);
                if (!sleBroker)
                    return false;
                std::uint32_t const loanSeq = sleBroker->at(sfLoanSequence);

                // Synthesize a Loan whose final scheduled payment lands
                // exactly at RedemptionDate: StartDate = red, interval = 60,
                // remaining = 1 => red + 60 >= red.
                auto sleLoan = std::make_shared<SLE>(
                    keylet::loan(closedEndedBrokerKeylet.key, SeqProxy::rawSequence(loanSeq)));
                sleLoan->at(sfLoanBrokerID) = closedEndedBrokerKeylet.key;
                sleLoan->at(sfLoanSequence) = loanSeq;
                sleLoan->at(sfBorrower) = a1.id();
                sleLoan->at(sfStartDate) = closedEndedRed;
                sleLoan->at(sfPaymentInterval) = 60;
                sleLoan->at(sfPaymentRemaining) = 1;
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                ac.view().insert(sleLoan);
                return true;
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const&, Env& env) -> bool {
                auto const sub = env.now().time_since_epoch().count() + 60;
                auto const red = sub + kMinInvestmentPeriod + 1'000'000;
                closedEndedRed = red;

                Vault const vault{env};
                auto [tx, keylet] = vault.create(
                    {.owner = a1,
                     .asset = xrpIssue(),
                     .vaultKind = closedEnded,
                     .subscriptionDate = sub,
                     .redemptionDate = red});
                env(tx);
                closedEndedKeylet = keylet;

                // Create the loan broker; LoanBrokerSet has no phase gate.
                closedEndedBrokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(env.seq(a1)));
                env(loan_broker::set(a1, keylet.key));

                // Advance parent close time into Investment so
                // ValidVault::finalizeLoanSet is satisfied.
                env.close(tp{d{sub + 1}});
                return true;
            });
    }

    void
    testMPT()
    {
        using namespace test::jtx;
        testcase << "MPT";

        MPTIssue const nonCanonicalMPTIssue{makeMptID(1, AccountID(0x4985601))};
        auto const nonCanonicalMPTAmount = [&](SField const& field) {
            return STAmount{
                field,
                nonCanonicalMPTIssue,
                kMaxMpTokenAmount + std::uint64_t{1},
                0,
                false,
                STAmount::Unchecked{}};
        };
        auto const negativeMPTAmount = [&](SField const& field) {
            return STAmount{field, nonCanonicalMPTIssue, 2, 0, true, STAmount::Unchecked{}};
        };
        auto const nonCanonicalMPTPayment = [&]() {
            return STTx{ttPAYMENT, [&](STObject& tx) {
                            tx.setFieldAmount(sfAmount, nonCanonicalMPTAmount(sfAmount));
                        }};
        };

        doInvariantCheck(
            makeEnv(defaultAmendments() - fixCleanup3_2_0),
            {},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{},
            nonCanonicalMPTPayment(),
            {tesSUCCESS, tesSUCCESS});

        doInvariantCheck(
            {{"ledger entry contains non-canonical MPT or XRP amount"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                auto sleNew = std::make_shared<SLE>(
                    keylet::check(a1.id(), SeqProxy::rawSequence((*sle)[sfSequence])));
                sleNew->setAccountID(sfAccount, a1.id());
                sleNew->setAccountID(sfDestination, a2.id());
                sleNew->setFieldAmount(sfSendMax, nonCanonicalMPTAmount(sfSendMax));
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"ledger entry contains non-canonical MPT or XRP amount"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                auto sleNew = std::make_shared<SLE>(
                    keylet::check(a1.id(), SeqProxy::rawSequence((*sle)[sfSequence])));
                sleNew->setAccountID(sfAccount, a1.id());
                sleNew->setAccountID(sfDestination, a2.id());
                sleNew->setFieldAmount(sfSendMax, negativeMPTAmount(sfSendMax));
                ac.view().insert(sleNew);
                return true;
            });

        // MPT OutstandingAmount > MaximumAmount
        doInvariantCheck(
            {{"OutstandingAmount overflow"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // mptissuance outstanding is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfOutstandingAmount, 110);
                sleNew->setFieldU64(sfMaximumAmount, 100);
                ac.view().insert(sleNew);
                return true;
            });

        // MPTToken amount doesn't add up to OutstandingAmount
        doInvariantCheck(
            {{"invalid OutstandingAmount balance"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                // mptissuance outstanding is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfOutstandingAmount, 100);
                sleNew->setFieldU64(sfMaximumAmount, 100);
                ac.view().insert(sleNew);

                sleNew = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a2));
                sleNew->setFieldU64(sfMPTAmount, 90);
                ac.view().insert(sleNew);

                return true;
            });

        // Overflow/Invalid balance on payment
        auto testPayment = [&](std::string const& log, auto&& update) {
            MPTID id;
            doInvariantCheck(
                {{log}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    return update(id, ac, a1);
                },
                XRPAmount{},
                STTx{ttPAYMENT, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const& a1, Account const& a2, Env& env) {
                    Account const gw("gw");
                    env.fund(XRP(1'000), gw);
                    MPTTester const mpt(
                        {.env = env, .issuer = gw, .holders = {a1}, .pay = 100, .maxAmt = 100});
                    id = mpt.issuanceID();
                    return true;
                });
        };
        testPayment(
            "invalid OutstandingAmount balance",
            [&](MPTID const& id, ApplyContext& ac, Account const& a1) {
                auto sle = ac.view().peek(keylet::mptoken(id, a1));
                if (!sle)
                    return false;
                sle->setFieldU64(sfMPTAmount, 101);
                ac.view().update(sle);
                return true;
            });
        testPayment(
            "OutstandingAmount overflow", [&](MPTID const& id, ApplyContext& ac, Account const&) {
                auto sle = ac.view().peek(keylet::mptokenIssuance(id));
                if (!sle)
                    return false;
                sle->setFieldU64(sfOutstandingAmount, 101);
                ac.view().update(sle);
                return true;
            });

        // The on-failure MPT checks (OutstandingAmount balance / transfer) apply
        // to every non-tesSUCCESS result, with no per-result exemption: on a tec
        // the transactor discards the view and re-applies only offer, trust
        // line, NFT offer and credential deletions, so an MPT change reaching
        // the invariant is a bug whatever the code. Seeded via initialResult.
        {
            MPTID id;
            // preclose: gw issues an MPT held by A1 and A2.
            auto const setup = [&](Account const& a1, Account const& a2, Env& env) {
                Account const gw("gw");
                env.fund(XRP(1'000), gw);
                MPTTester const mpt(
                    {.env = env, .issuer = gw, .holders = {a1, a2}, .pay = 50, .maxAmt = 1'000});
                id = mpt.issuanceID();
                return true;
            };

            // Consistent mint: OutstandingAmount and A1's balance both grow by
            // 10, so conservation holds and only the on-failure check fires.
            Precheck const mint = [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sleIss = ac.view().peek(keylet::mptokenIssuance(id));
                auto sleTok = ac.view().peek(keylet::mptoken(id, a1.id()));
                if (!sleIss || !sleTok)
                    return false;
                (*sleIss)[sfOutstandingAmount] = (*sleIss)[sfOutstandingAmount] + 10;
                (*sleTok)[sfMPTAmount] = (*sleTok)[sfMPTAmount] + 10;
                ac.view().update(sleIss);
                ac.view().update(sleTok);
                return true;
            };

            // Holder-to-holder transfer (A1 -> A2 by 10). OutstandingAmount is
            // unchanged, and CanTransfer keeps the ordinary transfer check
            // quiet, so only the on-failure check fires.
            Precheck const transfer = [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleIss = ac.view().peek(keylet::mptokenIssuance(id));
                auto sleA = ac.view().peek(keylet::mptoken(id, a1.id()));
                auto sleB = ac.view().peek(keylet::mptoken(id, a2.id()));
                if (!sleIss || !sleA || !sleB)
                    return false;
                (*sleIss)[sfFlags] = (*sleIss)[sfFlags] | lsfMPTCanTransfer;
                (*sleA)[sfMPTAmount] = (*sleA)[sfMPTAmount] - 10;
                (*sleB)[sfMPTAmount] = (*sleB)[sfMPTAmount] + 10;
                ac.view().update(sleIss);
                ac.view().update(sleA);
                ac.view().update(sleB);
                return true;
            };

            STTx const payment{ttPAYMENT, [](STObject&) {}};

            // Negative controls: nothing fires on tesSUCCESS. Without these, the
            // cases below would still pass if the result guard were dropped.
            doInvariantCheck({}, mint, XRPAmount{}, payment, {tesSUCCESS, tesSUCCESS}, setup);
            doInvariantCheck({}, transfer, XRPAmount{}, payment, {tesSUCCESS, tesSUCCESS}, setup);

            // tecKILLED and tecINCOMPLETE are not special: an MPT change paired
            // with either fires, as with any other failure.
            doInvariantCheck(
                {{"OutstandingAmount balance changed on failure"}},
                mint,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecKILLED);
            doInvariantCheck(
                {{"OutstandingAmount balance changed on failure"}},
                mint,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecINCOMPLETE);
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                transfer,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecKILLED);
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                transfer,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecINCOMPLETE);
            // The same change under a third failure result: the check keys off
            // "not tesSUCCESS", nothing finer.
            doInvariantCheck(
                {{"OutstandingAmount balance changed on failure"}},
                mint,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                transfer,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);

            // A lock moves value within one holder, so it is not a two-sided
            // transfer and the `senders || receivers` form is what catches it.
            // OutstandingAmount and the holder total are unchanged, so the
            // balance check stays quiet.
            Precheck const lock = [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sleTok = ac.view().peek(keylet::mptoken(id, a1.id()));
                if (!sleTok || (*sleTok)[sfMPTAmount] < 10)
                    return false;
                // A fresh MPToken has no locked amount, so set it directly.
                (*sleTok)[sfMPTAmount] = (*sleTok)[sfMPTAmount] - 10;
                sleTok->setFieldU64(sfLockedAmount, 10);
                ac.view().update(sleTok);
                return true;
            };
            // Negative control: a lock is legitimate on tesSUCCESS.
            doInvariantCheck({}, lock, XRPAmount{}, payment, {tesSUCCESS, tesSUCCESS}, setup);
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                lock,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecKILLED);
            // The lock is caught under any failure result.
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                lock,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);

            // A deleted MPToken has no amtAfter, so the sender/receiver counts
            // skip it and only the deletedAuthorized_ term can catch it. That
            // needs holders authorized but never paid, so the MPToken can be
            // erased with a zero balance and OutstandingAmount untouched --
            // otherwise the holder would register as a sender instead.
            MPTID emptyId;
            auto const setupEmpty = [&](Account const& a1, Account const& a2, Env& env) {
                Account const gw("gw");
                env.fund(XRP(1'000), gw);
                MPTTester const mpt({.env = env, .issuer = gw, .holders = {a1, a2}, .maxAmt = 100});
                emptyId = mpt.issuanceID();
                return true;
            };
            Precheck const eraseToken = [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sleTok = ac.view().peek(keylet::mptoken(emptyId, a1.id()));
                if (!sleTok || (*sleTok)[sfMPTAmount] != 0)
                    return false;
                ac.view().erase(sleTok);
                return true;
            };
            // ValidMPTIssuance also reports the deletion, so assert on
            // ValidMPTTransfer's message, which only the new check can produce.
            doInvariantCheck(
                {{"MPToken deleted on failure"}},
                eraseToken,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setupEmpty,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);
        }

        // Invalid IOU clawback delta must fail once MPTokensV2 enforces before/after validation.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback balance change is invalid"}},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;

                    STAmount balance{Issue{usd.currency, issuer.id()}, 80};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Full IOU clawback may delete the trustline; missing after-SLE represents zero balance.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;

                    ac.view().erase(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 100};
                    }},
                {tesSUCCESS, tesSUCCESS});
        }

        // Pre-MPTokensV2 invalid IOU clawback delta logs but remains non-enforcing.
        {
            Env env(*this, defaultAmendments() - featureMPTokensV2);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback balance change is invalid"}},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;

                    STAmount balance{Issue{usd.currency, issuer.id()}, 80};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tesSUCCESS, tesSUCCESS});
        }

        // Invalid MPT clawback delta must fail when raw MPToken debit mismatches sfAmount.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback balance change is invalid"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;

                    sleToken->setFieldU64(sfMPTAmount, 80);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 80);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // A clawback that mutates both IOU and MPT entries must fail under MPTokensV2.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline and MPToken both changed"}},
                [issuer, usd, id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleLine =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder.id()));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleLine || !sleToken || !sleIssuance)
                        return false;

                    STAmount balance{Issue{usd.currency, issuer.id()}, 90};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sleLine->setFieldAmount(sfBalance, balance);
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 90);
                    ac.view().update(sleLine);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Clawback that modifies a trustline other than the one implied by the
        // tx amount: clawbackTrustLineBalanceInHolderTerms returns nullopt for
        // the mismatched line.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            auto const eur = issuer["EUR"];
            env.trust(eur(100), holder);
            env(pay(issuer, holder, eur(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback changed the wrong line"}},
                [issuer, eur](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), eur.currency));
                    if (!sle)
                        return false;
                    STAmount balance{Issue{eur.currency, issuer.id()}, 90};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Clawback leaving the holder's balance negative.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline or MPT balance is negative"}},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;
                    // Make the holder's balance negative from their perspective.
                    STAmount balance{Issue{usd.currency, issuer.id()}, 80};
                    if (holder.id() < issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // IOU-amount clawback while only an MPToken changed: no trustline was
        // recorded, so iou_.before is empty.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback changed the wrong line"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 90);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Valid trustline change but a zero clawback amount.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback amount is invalid"}},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;
                    STAmount balance{Issue{usd.currency, issuer.id()}, 90};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 0};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // MPT clawback tx missing the Holder field.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback missing holder"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 90);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // MPT clawback where the holder's MPToken was deleted (after is empty).
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback token is missing"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    // Keep the issuance consistent after removing the token.
                    sleIssuance->setFieldU64(sfOutstandingAmount, 0);
                    ac.view().update(sleIssuance);
                    ac.view().erase(sleToken);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // MPT clawback that changed a different holder's MPToken.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env,
                 .issuer = issuer,
                 .holders = {holder, other},
                 .pay = 100,
                 .maxAmt = 200});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback changed the wrong token"}},
                [id](Account const&, Account const& other, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, other));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 190);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Valid MPToken change but a zero MPT clawback amount.
        {
            Env env(*this, defaultAmendments());
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback amount is invalid"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 90);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 0};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // More MPTokens created than expected
        std::array<std::pair<xrpl::TxType, std::uint8_t>, 4> const tests = {
            std::make_pair(ttAMM_WITHDRAW, 2),
            std::make_pair(ttAMM_CLAWBACK, 2),
            std::make_pair(ttAMM_CREATE, 3),
            std::make_pair(ttCHECK_CASH, 2)};
        for (auto const& [tx, nTokens] : tests)
        {
            doInvariantCheck(
                {{std::string("MPToken created for the MPT issuer")}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle = ac.view().peek(keylet::account(a1.id()));
                    if (!sle)
                        return false;

                    auto seq = sle->getFieldU32(sfSequence);
                    for (int i = 0; i < nTokens; ++i)
                    {
                        MPTIssue const mpt{makeMptID(seq + i, a1)};
                        auto sleNew =
                            std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                        ac.view().insert(sleNew);

                        sleNew = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a2));
                        ac.view().insert(sleNew);
                    }

                    return true;
                },
                XRPAmount{},
                STTx{tx, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // More MPTokens deleted than expected
        for (auto const& tx : {ttAMM_WITHDRAW, ttAMM_CLAWBACK})
        {
            MPTID id;
            Account const a3("A3");
            doInvariantCheck(
                {{"MPT authorize  succeeded but created/deleted bad number of mptokens"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    for (auto const& a : {a1, a2, a3})
                    {
                        auto sle = ac.view().peek(keylet::mptoken(id, a));
                        if (!sle)
                            return false;
                        ac.view().erase(sle);
                    }
                    return true;
                },
                XRPAmount{},
                STTx{tx, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const& a1, Account const& a2, Env& env) {
                    Account const gw("gw");
                    env.fund(XRP(1'000), gw, a3);
                    MPTTester const mpt({.env = env, .issuer = gw, .holders = {a1, a2, a3}});
                    id = mpt.issuanceID();
                    return true;
                });
        }

        // sfReferenceHolding can only be set on creation by VaultCreate. A
        // non-VaultCreate transaction that creates an MPTokenIssuance with
        // sfReferenceHolding present must trip the invariant.
        doInvariantCheck(
            {{"sfReferenceHolding set on a new MPTokenIssuance by a "
              "non-VaultCreate transaction"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sleAcct = ac.view().peek(keylet::account(a1.id()));
                if (!sleAcct)
                    return false;
                MPTIssue const mpt{makeMptID(sleAcct->getFieldU32(sfSequence), a1)};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldH256(sfReferenceHolding, uint256{1});
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}});

        // sfReferenceHolding is immutable: changing the field on an
        // existing MPTokenIssuance must trip the invariant. Set up a real
        // vault via preclose (so the share issuance carries
        // sfReferenceHolding), then mutate it in precheck to produce a
        // before/after pair.
        {
            uint256 vaultKey;
            doInvariantCheck(
                {{"sfReferenceHolding was modified on an existing "
                  "MPTokenIssuance"}},
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto const sleVault = ac.view().peek(keylet::vault(vaultKey));
                    if (!sleVault)
                        return false;
                    auto sleIssuance =
                        ac.view().peek(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
                    if (!sleIssuance)
                        return false;
                    sleIssuance->setFieldH256(sfReferenceHolding, uint256{2});
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const& a1, Account const&, Env& env) {
                    Account const issuer{"issuer"};
                    env.fund(XRP(10'000), issuer);
                    env.close();
                    MPTTester mptt{env, issuer, kMptInitNoFund};
                    mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
                    PrettyAsset const asset = mptt.issuanceID();
                    mptt.authorize({.account = a1});
                    env.close();

                    Vault const vault{env};
                    auto [tx, keylet] = vault.create({.owner = a1, .asset = asset});
                    env(tx);
                    env.close();
                    vaultKey = keylet.key;
                    return true;
                });
        }

        // A vault pseudo-account's MPToken cannot be deleted by anything
        // other than a VaultDelete transaction. Set up a vault, then have
        // an arbitrary tx erase the pseudo's MPToken in precheck.
        {
            uint256 vaultKey;
            doInvariantCheck(
                {{"vault pseudo-account holding deleted by a "
                  "non-VaultDelete transaction"}},
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto const sleVault = ac.view().peek(keylet::vault(vaultKey));
                    if (!sleVault)
                        return false;
                    auto const sleIssuance =
                        ac.view().peek(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
                    if (!sleIssuance || !sleIssuance->isFieldPresent(sfReferenceHolding))
                        return false;
                    auto sleHolding = ac.view().peek(
                        keylet::unchecked(sleIssuance->getFieldH256(sfReferenceHolding)));
                    if (!sleHolding)
                        return false;
                    ac.view().erase(sleHolding);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const& a1, Account const&, Env& env) {
                    Account const issuer{"issuer"};
                    env.fund(XRP(10'000), issuer);
                    env.close();
                    MPTTester mptt{env, issuer, kMptInitNoFund};
                    mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
                    PrettyAsset const asset = mptt.issuanceID();
                    mptt.authorize({.account = a1});
                    env.close();

                    Vault const vault{env};
                    auto [tx, keylet] = vault.create({.owner = a1, .asset = asset});
                    env(tx);
                    env.close();
                    vaultKey = keylet.key;
                    return true;
                });
        }

        // Invalid transfer
        std::array<std::pair<TxType, bool>, 3> const invalidTransferTests = {
            std::make_pair(ttAMM_WITHDRAW, false),
            std::make_pair(ttPAYMENT, false),
            std::make_pair(ttPAYMENT, true)};
        // The two amendments that gate enforcement, in all four combinations.
        FeatureBitset const gatesEnabled{featureMPTokensV2, fixCleanup3_4_0};
        for (auto const gates :
             {gatesEnabled,
              gatesEnabled - featureMPTokensV2,
              gatesEnabled - fixCleanup3_4_0,
              FeatureBitset{}})
        {
            for (auto const& [tx, crossCurrencyPayment] : invalidTransferTests)
            {
                for (auto const flag :
                     {static_cast<std::uint32_t>(lsfMPTLocked),
                      ~lsfMPTCanTransfer,
                      ~lsfMPTCanTrade,
                      0u})
                {
                    MPTID id{};
                    auto const isSuccess = !gates.any() || flag == 0 ||
                        (tx == ttPAYMENT && !crossCurrencyPayment && (flag == ~lsfMPTCanTrade)) ||
                        (tx == ttAMM_WITHDRAW &&
                         (flag == ~lsfMPTCanTrade || flag == ~lsfMPTCanTransfer));
                    std::pair<TER, TER> const error = isSuccess
                        ? std::make_pair(TER(tesSUCCESS), TER(tesSUCCESS))
                        : std::make_pair(TER(tecINVARIANT_FAILED), TER(tefINVARIANT_FAILED));
                    doInvariantCheck(
                        {{isSuccess ? "" : "invalid MPToken transfer between holders"}},
                        [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                            auto update = [&](AccountID const& a, std::uint64_t v) {
                                auto sle = ac.view().peek(keylet::mptoken(id, a));
                                if (!sle)
                                    return false;
                                sle->at(sfMPTAmount) = v;
                                ac.view().update(sle);
                                return true;
                            };
                            auto issuanceSle = ac.view().peek(keylet::mptokenIssuance(id));
                            if (!issuanceSle)
                                return false;
                            auto const flags = issuanceSle->at(sfFlags);
                            if (flag == lsfMPTLocked)
                            {
                                issuanceSle->at(sfFlags) = flags | lsfMPTLocked;
                            }
                            else if (flag != 0u)
                            {
                                issuanceSle->at(sfFlags) = flags & flag;
                            }
                            issuanceSle->at(sfOutstandingAmount) = 200;
                            ac.view().update(issuanceSle);
                            return update(a1, 101) && update(a2, 99);
                        },
                        XRPAmount{},
                        STTx{
                            tx,
                            [&](STObject& tx) {
                                if (crossCurrencyPayment)
                                {
                                    tx.setFieldAmount(
                                        sfSendMax, STAmount(MPTAmount{100}, MPTIssue{id}));
                                }
                            }},
                        {error.first, error.second},
                        [&](Account const& a1, Account const& a2, Env& env) {
                            Account const gw("gw");
                            env.fund(XRP(1'000), gw);
                            MPTTester const usd(
                                {.env = env, .issuer = gw, .holders = {a1, a2}, .pay = 100});
                            id = usd.issuanceID();
                            // Either gate enforces, so both must be off to stay
                            // advisory. Disable after setting up the MPT; the
                            // next env.close() is what makes it take effect.
                            if (!gates[featureMPTokensV2])
                                env.disableFeature(featureMPTokensV2);
                            if (!gates[fixCleanup3_4_0])
                                env.disableFeature(fixCleanup3_4_0);
                            return true;
                        });
                }
            }
        }

        // An orphan has a zero balance, so only deletion is legitimate (see
        // "Skipping Deleted MPTs" in testConfidentialMPTTransfer).
        {
            MPTID orphanID;
            auto const setupOrphan = [&](Account const& a1, Account const& a2, Env& env) {
                MPTTester mpt(env, a1, {.holders = {a2}, .fund = false});
                mpt.create({.flags = tfMPTCanTransfer});
                orphanID = mpt.issuanceID();
                // A2 is authorized but never paid, so its balance is zero and
                // the issuance can be destroyed while its MPToken lives on.
                mpt.authorize({.account = a2});
                mpt.destroy();
                return true;
            };
            // ValidMPTBalanceChanges also reports this, so assert on the
            // orphan message, which only the missing-issuance branch produces.
            doInvariantCheck(
                {{"orphaned MPToken balance changed"}},
                [&](Account const&, Account const& a2, ApplyContext& ac) {
                    auto sleTok = ac.view().peek(keylet::mptoken(orphanID, a2.id()));
                    if (!sleTok || (*sleTok)[sfMPTAmount] != 0)
                        return false;
                    (*sleTok)[sfMPTAmount] = (*sleTok)[sfMPTAmount] + 10;
                    ac.view().update(sleTok);
                    return true;
                },
                XRPAmount{},
                STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setupOrphan);
            // Negative control: erasing the orphan is how it gets cleaned up.
            doInvariantCheck(
                {},
                [&](Account const&, Account const& a2, ApplyContext& ac) {
                    auto sleTok = ac.view().peek(keylet::mptoken(orphanID, a2.id()));
                    if (!sleTok)
                        return false;
                    ac.view().erase(sleTok);
                    return true;
                },
                XRPAmount{},
                STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
                {tesSUCCESS, tesSUCCESS},
                setupOrphan);
            // The same erase on a failure. The orphan branch continues, so only
            // the pre-loop deletion check can report this one.
            doInvariantCheck(
                {{"MPToken deleted on failure"}},
                [&](Account const&, Account const& a2, ApplyContext& ac) {
                    auto sleTok = ac.view().peek(keylet::mptoken(orphanID, a2.id()));
                    if (!sleTok)
                        return false;
                    ac.view().erase(sleTok);
                    return true;
                },
                XRPAmount{},
                STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setupOrphan,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);
        }

        // Vault-share freeze invariant: isVaultPseudoAccountFrozen descends
        // through sfReferenceHolding to test the vault's underlying asset for
        // each changed holder.
        {
            Account const gw{"gw"};
            MPTID shareID{};

            // Vault setup: a1 and a2 both deposit IOU and hold vault shares.
            auto const setupVault = [&](Account const& a1,
                                        Account const& a2,
                                        Env& env) -> std::tuple<MPTID, AccountID> {
                env.fund(XRP(1'000), gw);
                env.trust(gw["IOU"](10'000), a1);
                env.trust(gw["IOU"](10'000), a2);
                env.close();
                env(pay(gw, a1, gw["IOU"](500)));
                env(pay(gw, a2, gw["IOU"](500)));
                env.close();

                Vault const vault{env};
                auto [createTx, vaultKeylet] = vault.create({.owner = a1, .asset = gw["IOU"]});
                env(createTx);
                env.close();
                env(vault.deposit(
                    {.depositor = a1, .id = vaultKeylet.key, .amount = gw["IOU"](100)}));
                env(vault.deposit(
                    {.depositor = a2, .id = vaultKeylet.key, .amount = gw["IOU"](100)}));
                env.close();

                return {env.le(vaultKeylet)->at(sfShareMPTID), env.le(vaultKeylet)->at(sfAccount)};
            };

            // Simulate a vault-share transfer: a1 sends 10 shares to a2.
            auto const precheck =
                [&](Account const& a1, Account const& a2, ApplyContext& ac) -> bool {
                auto sle1 = ac.view().peek(keylet::mptoken(shareID, a1.id()));
                auto sle2 = ac.view().peek(keylet::mptoken(shareID, a2.id()));
                if (!sle1 || !sle2)
                    return false;
                (*sle1)[sfMPTAmount] -= 10;
                (*sle2)[sfMPTAmount] += 10;
                ac.view().update(sle1);
                ac.view().update(sle2);
                return true;
            };

            // Case: vault pseudo-account's IOU trustline is frozen.
            {
                auto const preclose = [&](Account const& a1, Account const& a2, Env& env) -> bool {
                    auto [sid, vid] = setupVault(a1, a2, env);
                    shareID = sid;
                    env(trust(gw, gw["IOU"](0), Account{"vaultPseudo", vid}, tfSetFreeze));
                    env.close();
                    return true;
                };

                doInvariantCheck(
                    Env{*this, defaultAmendments()},
                    {{"invalid MPToken transfer between holders"}},
                    precheck,
                    XRPAmount{},
                    STTx{ttPAYMENT, [](STObject&) {}},
                    {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                    preclose);
            }

            // Case: receiver's (a2's) IOU trustline is frozen.
            {
                auto const preclose = [&](Account const& a1, Account const& a2, Env& env) -> bool {
                    auto [sid, vid] = setupVault(a1, a2, env);
                    shareID = sid;
                    env(trust(gw, gw["IOU"](0), a2, tfSetFreeze));
                    env.close();
                    return true;
                };

                doInvariantCheck(
                    Env{*this, defaultAmendments()},
                    {{"invalid MPToken transfer between holders"}},
                    precheck,
                    XRPAmount{},
                    STTx{ttPAYMENT, [](STObject&) {}},
                    {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                    preclose);
            }
        }
    }

    void
    testAMM()
    {
        testcase << "AMM";
        using namespace jtx;

        MPTID mptID{};
        uint256 ammID{};
        AccountID ammAccountID{};
        Account const gw{"gw"};
        Issue lptIssue{};
        PrettyAsset poolAsset{xrpIssue()};

        auto deleteAMMAccount = [&](ApplyContext& ac, bool) {
            auto sle = ac.view().peek(keylet::account(ammAccountID));
            if (!sle)
                return false;
            ac.view().erase(sle);
            return true;
        };

        auto updateLPTokensBalance = [&](ApplyContext& ac, std::int64_t amount) {
            auto sle = ac.view().peek(keylet::amm(ammID));
            if (!sle)
                return false;
            sle->setFieldAmount(sfLPTokenBalance, STAmount{lptIssue, amount});
            ac.view().update(sle);
            return true;
        };
        auto updateLPTokensBadAmount = [&](ApplyContext& ac, bool) {
            return updateLPTokensBalance(ac, -1);
        };
        auto updateLPTokensBadBalance = [&](ApplyContext& ac, bool) {
            return updateLPTokensBalance(ac, 200'000'000);
        };
        auto updateAMM = [&](ApplyContext& ac, bool) { return updateLPTokensBalance(ac, 10); };

        auto updateAMMPool = [&](ApplyContext& ac, bool isMPT) {
            if (isMPT)
            {
                auto sle = ac.view().peek(keylet::mptoken(mptID, ammAccountID));
                if (!sle)
                    return false;
                sle->setFieldU64(sfMPTAmount, 1);
                ac.view().update(sle);
                return true;
            }
            auto sle = ac.view().peek(keylet::account(ammAccountID));
            if (!sle)
                return false;
            sle->setFieldAmount(sfBalance, XRP(1));
            ac.view().update(sle);
            return true;
        };

        auto test = [&](auto const txType,
                        auto&& update,
                        bool isMPT,
                        TER error = tecINVARIANT_FAILED) {
            doInvariantCheck(
                {{"AMM"}},
                [&](Account const&, Account const&, ApplyContext& ac) { return update(ac, isMPT); },
                XRPAmount{},
                STTx{txType, [&](STObject& tx) {}},
                {tecINVARIANT_FAILED, error},
                [&](Account const&, Account const&, Env& env) {
                    env.fund(XRP(1'000), gw);
                    poolAsset = [&]() -> PrettyAsset {
                        if (isMPT)
                        {
                            MPT const mpt = MPTTester({.env = env, .issuer = gw});
                            mptID = mpt.issuanceID;
                            return mpt;
                        }
                        return gw["USD"];
                    }();
                    AMM const amm(env, gw, XRP(100), poolAsset(100));
                    ammAccountID = amm.ammAccount();
                    ammID = amm.ammID();
                    lptIssue = amm.lptIssue();
                    return true;
                });
        };

        for (bool const isMPT : {false, true})
        {
            // Under fixCleanup3_4_0 the MPT balance invariants also fire on the
            // second pass, so both IOU and MPT pools now escalate to tef.
            auto const error = TER(tefINVARIANT_FAILED);
            for (auto txType : {ttAMM_CREATE, ttAMM_DEPOSIT, ttAMM_CLAWBACK, ttAMM_WITHDRAW})
            {
                test(txType, deleteAMMAccount, isMPT, tefINVARIANT_FAILED);
                test(txType, updateLPTokensBadAmount, isMPT);
                test(txType, updateLPTokensBadBalance, isMPT);
            }
            for (auto txType : {ttAMM_BID, ttAMM_VOTE})
            {
                test(txType, updateAMMPool, isMPT, error);
                test(txType, updateLPTokensBadAmount, isMPT);
                test(txType, updateLPTokensBadBalance, isMPT);
            }
            for (auto txType : {ttAMM_DELETE, ttCHECK_CASH, ttOFFER_CREATE, ttPAYMENT})
            {
                test(txType, updateAMM, isMPT);
            }
        }
    }

    // Test the invariant overwrite fix for both pre- and post-amendment
    // behavior. With the fix enabled, |= accumulates violations across
    // entries so a later valid entry cannot clear an earlier violation.
    // Without the fix, = assignment means the last-visited entry wins.
    void
    testInvariantOverwrite(FeatureBitset features)
    {
        using namespace test::jtx;
        bool const fixEnabled = features[fixCleanup3_1_3];
        std::initializer_list<TER> const failTers = {tecINVARIANT_FAILED, tefINVARIANT_FAILED};
        std::initializer_list<TER> const passTers = {tesSUCCESS, tesSUCCESS};

        // Insert two trust line SLEs in hash-sorted order, with the "bad"
        // entry at the lower-sorting key so it is visited first by
        // ApplyStateTable::visit(). The configurer callables receive the
        // SLE and the Issue corresponding to that side's keylet currency.
        auto const insertOrderedTrustLinePair = [](ApplyContext& ac,
                                                   Account const& a1,
                                                   Account const& a2,
                                                   Account const& a3,
                                                   auto const& badConfig,
                                                   auto const& goodConfig) {
            char const* const c1 = "USD";
            char const* const c2 = "EUR";
            auto const k1 = keylet::trustLine(a1, a2, a1[c1].currency);
            auto const k2 = keylet::trustLine(a1, a3, a1[c2].currency);

            bool const k1First = k1.key < k2.key;
            auto const& badKey = k1First ? k1 : k2;
            auto const& goodKey = k1First ? k2 : k1;
            Issue const badIss{k1First ? a1[c1].currency : a1[c2].currency, a1.id()};
            Issue const goodIss{k1First ? a1[c2].currency : a1[c1].currency, a1.id()};

            auto const sleBad = std::make_shared<SLE>(badKey);
            badConfig(*sleBad, badIss);
            ac.view().insert(sleBad);

            auto const sleGood = std::make_shared<SLE>(goodKey);
            goodConfig(*sleGood, goodIss);
            ac.view().insert(sleGood);
        };

        // Regression: bad XRP trust line followed by a valid trust line.
        // With the fix, the invariant catches the violation. Without it,
        // the valid entry overwrites the flag to false. The keylet
        // currencies are non-XRP (the invariant inspects sfLowLimit /
        // sfHighLimit issue, not the keylet currency).
        testcase << "overwrite: NoXRPTrustLines" + std::string(fixEnabled ? " fix" : "");
        doInvariantCheck(
            makeEnv(features),
            fixEnabled ? std::vector<std::string>{{"an XRP trust line was created"}}
                       : std::vector<std::string>{},
            [&insertOrderedTrustLinePair](Account const& a1, Account const& a2, ApplyContext& ac) {
                Account const a3{"A3"};
                insertOrderedTrustLinePair(
                    ac,
                    a1,
                    a2,
                    a3,
                    [](SLE& sle, Issue const& iss) {
                        // sfLowLimit has xrpIssue, making isXrp = true
                        sle.setFieldAmount(sfLowLimit, STAmount{xrpIssue(), 0});
                        sle.setFieldAmount(sfHighLimit, STAmount{iss, 0});
                    },
                    [](SLE& sle, Issue const& iss) {
                        sle.setFieldAmount(sfLowLimit, STAmount{iss, 0});
                        sle.setFieldAmount(sfHighLimit, STAmount{iss, 0});
                    });
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            fixEnabled ? failTers : passTers);

        // Regression: bad deep-freeze trust line followed by a valid one.
        testcase << "overwrite: NoDeepFreeze" + std::string(fixEnabled ? " fix" : "");
        doInvariantCheck(
            makeEnv(features),
            fixEnabled ? std::vector<std::string>{{"a trust line with deep freeze flag without "
                                                   "normal freeze was created"}}
                       : std::vector<std::string>{},
            [&insertOrderedTrustLinePair](Account const& a1, Account const& a2, ApplyContext& ac) {
                Account const a3{"A3"};
                insertOrderedTrustLinePair(
                    ac,
                    a1,
                    a2,
                    a3,
                    [](SLE& sle, Issue const& iss) {
                        sle.setFieldAmount(sfLowLimit, STAmount{iss, 0});
                        sle.setFieldAmount(sfHighLimit, STAmount{iss, 0});
                        sle.setFieldU32(sfFlags, lsfLowDeepFreeze);
                    },
                    [](SLE& sle, Issue const& iss) {
                        sle.setFieldAmount(sfLowLimit, STAmount{iss, 0});
                        sle.setFieldAmount(sfHighLimit, STAmount{iss, 0});
                        sle.setFieldU32(sfFlags, 0u);
                    });
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            fixEnabled ? failTers : passTers);

        // Regression: MPT OutstandingAmount exceeds max, but locked <=
        // outstanding. Plain assignment would overwrite bad_ = true.
        // With the fix, NoZeroEscrow catches it.
        // Without the fix, NoZeroEscrow passes but ValidMPTIssuance
        // still fires ("a MPT issuance was created").
        testcase << "overwrite: NoZeroEscrow MPT" + std::string(fixEnabled ? " fix" : "");
        doInvariantCheck(
            makeEnv(features),
            fixEnabled ? std::vector<std::string>{{"escrow specifies invalid amount"}}
                       : std::vector<std::string>{{"a MPT issuance was created"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(1, AccountID(0x4985601))};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                // outstanding exceeds kMaxMpTokenAmount -> checkAmount sets bad_
                sleNew->setFieldU64(sfOutstandingAmount, kMaxMpTokenAmount + 1);
                // locked is valid and <= outstanding -> must NOT clear bad_
                sleNew->setFieldU64(sfLockedAmount, 10);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            failTers);
    }

    void
    testVaultComputeCoarsestScale()
    {
        using namespace jtx;

        Account const issuer{"issuer"};
        PrettyAsset const vaultAsset = issuer["IOU"];

        struct TestCase
        {
            std::string name;
            std::int32_t expectedMinScale;
            std::vector<ValidVault::DeltaInfo> values;
        };

        for (auto const mantissaScale : MantissaRange::getAllScales())
        {
            if (mantissaScale == MantissaRange::MantissaScale::Small)
                continue;
            NumberMantissaScaleGuard const g{mantissaScale};

            auto makeDelta = [&vaultAsset](Number const& n) -> ValidVault::DeltaInfo {
                return {.delta = n, .scale = scale(n, vaultAsset.raw())};
            };

            auto const testCases = std::vector<TestCase>{
                {
                    .name = "No values",
                    .expectedMinScale = 0,
                    .values = {},
                },
                {
                    .name = "Mixed integer and Number values",
                    .expectedMinScale = -15,
                    .values = {makeDelta(1), makeDelta(-1), makeDelta(Number{10, -1})},
                },
                {
                    .name = "Mixed scales",
                    .expectedMinScale = -17,
                    .values =
                        {makeDelta(Number{1, -2}),
                         makeDelta(Number{5, -3}),
                         makeDelta(Number{3, -2})},
                },
                {
                    .name = "Equal scales",
                    .expectedMinScale = -16,
                    .values =
                        {makeDelta(Number{1, -1}),
                         makeDelta(Number{5, -1}),
                         makeDelta(Number{1, -1})},
                },
                {
                    .name = "Mixed mantissa sizes",
                    .expectedMinScale = -12,
                    .values =
                        {makeDelta(Number{1}),
                         makeDelta(Number{1234, -3}),
                         makeDelta(Number{12345, -6}),
                         makeDelta(Number{123, 1})},
                },
            };

            for (auto const& tc : testCases)
            {
                testcase("vault computeCoarsestScale: " + tc.name);

                auto const actualScale = ValidVault::computeCoarsestScale(tc.values);

                BEAST_EXPECTS(
                    actualScale == tc.expectedMinScale,
                    "expected: " + std::to_string(tc.expectedMinScale) +
                        ", actual: " + std::to_string(actualScale));
                for (auto const& num : tc.values)
                {
                    // None of these scales are far enough apart that rounding the
                    // values would lose information, so check that the rounded
                    // value matches the original.
                    auto const actualRounded = roundToAsset(vaultAsset, num.delta, actualScale);
                    BEAST_EXPECTS(
                        actualRounded == num.delta,
                        "number " + to_string(num.delta) + " rounded to scale " +
                            std::to_string(actualScale) + " is " + to_string(actualRounded));
                }
            }

            auto const testCases2 = std::vector<TestCase>{
                {
                    .name = "False equivalence",
                    .expectedMinScale = -15,
                    .values =
                        {
                            makeDelta(Number{1234567890123456789, -18}),
                            makeDelta(Number{12345, -4}),
                            makeDelta(Number{1}),
                        },
                },
            };

            // Unlike the first set of test cases, the values in these test could
            // look equivalent if using the wrong scale.
            for (auto const& tc : testCases2)
            {
                testcase("vault computeCoarsestScale: " + tc.name);

                auto const actualScale = ValidVault::computeCoarsestScale(tc.values);

                BEAST_EXPECTS(
                    actualScale == tc.expectedMinScale,
                    "expected: " + std::to_string(tc.expectedMinScale) +
                        ", actual: " + std::to_string(actualScale));
                std::optional<Number> first;
                Number firstRounded;
                for (auto const& num : tc.values)
                {
                    if (!first)
                    {
                        first = num.delta;
                        firstRounded = roundToAsset(vaultAsset, num.delta, actualScale);
                        continue;
                    }
                    auto const numRounded = roundToAsset(vaultAsset, num.delta, actualScale);
                    BEAST_EXPECTS(
                        numRounded != firstRounded,
                        "at a scale of " + std::to_string(actualScale) + " " +
                            to_string(num.delta) + " == " + to_string(*first));
                }
            }
        }
    }

    void
    testSponsorship()
    {
        using namespace test::jtx;
        using namespace std::string_literals;
        testcase("Sponsorship");
        {
            auto const expectMessage =
                "SponsoredOwnerCount does not equal SponsoringOwnerCount delta.";

            doInvariantCheck(
                {{expectMessage}}, [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle = ac.view().peek(keylet::account(a1.id()));
                    if (!sle)
                        return false;
                    sle->setFieldU32(sfSponsoredOwnerCount, 1);
                    ac.view().update(sle);
                    return true;
                });

            doInvariantCheck(
                {{expectMessage}}, [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle = ac.view().peek(keylet::account(a1.id()));
                    if (!sle)
                        return false;
                    sle->setFieldU32(sfSponsoringOwnerCount, 1);
                    ac.view().update(sle);
                    return true;
                });
        }

        {
            auto const expectMessage =
                "OwnerCount must be greater than or equal to SponsoredOwnerCount.";

            doInvariantCheck(
                {{expectMessage}}, [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle = ac.view().peek(keylet::account(a1.id()));
                    if (!sle)
                        return false;
                    sle->setFieldU32(sfOwnerCount, 0);
                    sle->setFieldU32(sfSponsoredOwnerCount, 1);
                    ac.view().update(sle);

                    auto const sle2 = ac.view().peek(keylet::account(a2.id()));
                    if (!sle2)
                        return false;
                    sle2->setFieldU32(sfSponsoringOwnerCount, 1);
                    ac.view().update(sle2);
                    return true;
                });
        }

        {
            auto const expectMessage =
                "SponsoredObjectOwnerCount does not equal SponsoredOwnerCount delta.";
            uint256 checkID;

            doInvariantCheck(
                {{expectMessage}},
                [&](Account const&, Account const& a2, ApplyContext& ac) {
                    auto const check = ac.view().peek(keylet::check(checkID));
                    if (!check)
                        return false;
                    check->setAccountID(sfSponsor, a2.id());
                    ac.view().update(check);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&checkID](Account const& a1, Account const& a2, Env& env) {
                    checkID = keylet::check(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;
                    env(check::create(a1, a2, XRP(1)));
                    return true;
                });
        }

        {
            auto const expectMessage =
                "Invariant failed: Net delta of SponsoringAccountCount does "
                "not match net delta of sfSponsor presence.";

            doInvariantCheck(
                {{expectMessage}}, [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle = ac.view().peek(keylet::account(a1.id()));
                    if (!sle)
                        return false;
                    sle->setFieldU32(sfSponsoringAccountCount, 1);
                    ac.view().update(sle);
                    return true;
                });

            doInvariantCheck(
                {{expectMessage}}, [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle = ac.view().peek(keylet::account(a1.id()));
                    if (!sle)
                        return false;
                    sle->setAccountID(sfSponsor, a2.id());
                    ac.view().update(sle);
                    return true;
                });
        }
    }

    void
    testObjectHasPseudoAccount()
    {
        testcase << "object has pseudo-account";
        using namespace jtx;

        auto const amendments = defaultAmendments() | fixCleanup3_3_0;

        // Vault: object deleted without its pseudo-account
        {
            Keylet vaultKeylet = keylet::amendments();
            doInvariantCheck(
                Env{*this, amendments},
                {{"deleted Vault without deleting its pseudo-account"}},
                [&vaultKeylet](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(vaultKeylet);
                    if (!sle)
                        return false;
                    ac.view().erase(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttVAULT_DELETE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&vaultKeylet](Account const& a1, Account const&, Env& env) {
                    Vault const vault{env};
                    auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                    env(tx);
                    vaultKeylet = keylet;
                    return true;
                });
        }

        // AMM: object deleted without its pseudo-account
        {
            uint256 ammID{};
            Account const gw{"gw"};
            doInvariantCheck(
                Env{*this, amendments},
                {{"deleted AMM without deleting its pseudo-account"}},
                [&ammID](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::amm(ammID));
                    if (!sle)
                        return false;
                    ac.view().erase(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttAMM_DELETE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&ammID, &gw](Account const&, Account const&, Env& env) {
                    env.fund(XRP(1'000), gw);
                    AMM const amm(env, gw, XRP(100), gw["USD"](100));
                    ammID = amm.ammID();
                    return true;
                });
        }

        // LoanBroker: object deleted without its pseudo-account
        {
            Keylet loanBrokerKeylet = keylet::amendments();
            doInvariantCheck(
                Env{*this, amendments},
                {{"deleted LoanBroker without deleting its pseudo-account"}},
                [&loanBrokerKeylet](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(loanBrokerKeylet);
                    if (!sle)
                        return false;
                    ac.view().erase(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_DELETE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&loanBrokerKeylet, this](Account const& a1, Account const&, Env& env) {
                    PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
                    loanBrokerKeylet = this->createLoanBroker(a1, env, xrpAsset);
                    return BEAST_EXPECT(env.le(loanBrokerKeylet));
                });
        }

        // Deleted object missing sfAccount field (defensive check).
        // Manually construct the view to place a vault SLE without
        // sfAccount into the base ledger, then erase it.
        {
            Env env{*this, amendments};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            auto sleVault = std::make_shared<SLE>(vaultKeylet);
            sleVault->makeFieldAbsent(sfAccount);
            ov.rawInsert(sleVault);

            STTx const tx{ttVAULT_DELETE, [](STObject&) {}};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            auto sle = ac.view().peek(vaultKeylet);
            if (!BEAST_EXPECT(sle))
                return;
            ac.view().erase(sle);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains("is missing pseudo-account field"));
        }
    }

    void
    testTxCheckException()
    {
        testcase << "txCheck exception";
        using namespace jtx;

        // A TxInvariantCheck that throws from the requested hook, so we can
        // exercise checkInvariantsHelper's catch block via the
        // transaction-specific layer (as opposed to the protocol layer,
        // which testObjectHasPseudoAccount's last case already covers via a
        // real Transactor's finalizeInvariants).
        enum class ThrowFrom { VisitEntry, Finalize };

        struct ThrowingTxInvariantCheck : TxInvariantCheck
        {
            ThrowFrom const throwFrom;

            explicit ThrowingTxInvariantCheck(ThrowFrom throwFrom) : throwFrom(throwFrom)
            {
            }

            void
            visitEntry(bool, SLE::const_ref, SLE::const_ref) override
            {
                if (throwFrom == ThrowFrom::VisitEntry)
                    throw std::runtime_error("test-injected visitEntry exception");
            }

            [[nodiscard]] bool
            finalize(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&) override
            {
                if (throwFrom == ThrowFrom::Finalize)
                    throw std::runtime_error("test-injected finalize exception");
                return true;
            }
        };

        for (auto const throwFrom : {ThrowFrom::VisitEntry, ThrowFrom::Finalize})
        {
            Env env{*this};
            Account const alice{"alice"};
            env.fund(XRP(1000), alice);
            env.close();

            OpenView ov{*env.current()};
            STTx const tx{ttACCOUNT_SET, [](STObject&) {}};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // visitEntry only runs for entries the transaction touched, so
            // make a modification for the traversal to report.
            auto sle = ac.view().peek(keylet::account(alice.id()));
            if (!BEAST_EXPECT(sle))
                return;
            sle->at(sfSequence) = sle->at(sfSequence) + 1;
            ac.view().update(sle);

            ThrowingTxInvariantCheck throwing{throwFrom};
            TER terActual = tesSUCCESS;
            for (TER const& terExpect : {TER(tecINVARIANT_FAILED), TER(tefINVARIANT_FAILED)})
            {
                terActual = checkInvariants(ac, terActual, XRPAmount{}, throwing);
                BEAST_EXPECT(terExpect == terActual);
                BEAST_EXPECT(sink.messages().str().contains(
                    "Transaction caused an exception during invariant checks"));
            }
        }
    }

    void
    testTxCheckFinalizeFalse()
    {
        testcase << "txCheck finalize returns false";
        using namespace jtx;

        // A TxInvariantCheck whose finalize returns false, so we can exercise
        // the "Transaction has failed one or more transaction invariants"
        // log path in checkInvariantsHelper independently of any real
        // transactor. This is the transaction-layer analogue of the
        // protocol-layer coverage in testObjectHasPseudoAccount / others.
        struct FailingTxInvariantCheck : TxInvariantCheck
        {
            void
            visitEntry(bool, SLE::const_ref, SLE::const_ref) override
            {
            }

            [[nodiscard]] bool
            finalize(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&) override
            {
                return false;
            }
        };

        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();

        OpenView ov{*env.current()};
        STTx const tx{ttACCOUNT_SET, [](STObject&) {}};
        test::StreamSink sink{beast::Severity::Warning};
        beast::Journal const jlog{sink};
        ApplyContext ac{env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
        CurrentTransactionRulesGuard const rulesGuard(ov.rules());

        FailingTxInvariantCheck failing;
        TER terActual = tesSUCCESS;
        for (TER const& terExpect : {TER(tecINVARIANT_FAILED), TER(tefINVARIANT_FAILED)})
        {
            terActual = checkInvariants(ac, terActual, XRPAmount{}, failing);
            BEAST_EXPECT(terExpect == terActual);
            BEAST_EXPECT(sink.messages().str().contains(
                "Transaction has failed one or more transaction invariants"));
            // The protocol-layer log must not appear: only the tx-layer
            // finalize failed here.
            BEAST_EXPECT(!sink.messages().str().contains(
                "Transaction has failed one or more global invariants"));
        }
    }

    void
    testConfidentialMPTTransfer()
    {
        using namespace test::jtx;
        testcase << "ValidConfidentialMPToken";

        MPTID mptID;

        // Generate an MPT with privacy, issue 100 tokens to A2.
        // Perform a confidential conversion to populate encrypted state.
        auto const precloseConfidential =
            [&mptID](Account const& a1, Account const& a2, Env& env) -> bool {
            MPTTester mpt(env, a1, {.holders = {a2}, .fund = false});
            mpt.create({.flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
            mptID = mpt.issuanceID();

            mpt.authorize({.account = a2});
            mpt.pay(a1, a2, 100);

            mpt.generateKeyPair(a1);
            mpt.set({.account = a1, .issuerPubKey = mpt.getPubKey(a1)});

            mpt.generateKeyPair(a2);
            mpt.convert({
                .account = a2,
                .amt = 100,
                .holderPubKey = mpt.getPubKey(a2),
            });
            return true;
        };

        // badDelete
        doInvariantCheck(
            {"MPToken deleted with encrypted fields while COA > 0"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                // Force an erase of the object while the COA remains 100
                ac.view().erase(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseConfidential);

        // badConsistency
        doInvariantCheck(
            {"MPToken encrypted field existence inconsistency"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                // Remove one of the required encrypted fields to create a mismatch
                sleToken->makeFieldAbsent(sfIssuerEncryptedBalance);
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        doInvariantCheck(
            {"MPToken encrypted field existence inconsistency"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                sleToken->makeFieldAbsent(sfIssuerEncryptedBalance);
                sleToken->makeFieldAbsent(sfConfidentialBalanceInbox);
                sleToken->makeFieldAbsent(sfConfidentialBalanceSpending);
                sleToken->setFieldVL(sfAuditorEncryptedBalance, Blob{0x00});
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // requiresPrivacyFlag
        auto const precloseNoPrivacy = [&mptID](
                                           Account const& a1, Account const& a2, Env& env) -> bool {
            MPTTester mpt(env, a1, {.holders = {a2}, .fund = false});
            // completely omitted the tfMPTCanHoldConfidentialBalance flag here.
            mpt.create({.flags = tfMPTCanTransfer});
            mptID = mpt.issuanceID();
            mpt.authorize({.account = a2});
            mpt.pay(a1, a2, 100);
            return true;
        };

        doInvariantCheck(
            {"MPToken has encrypted fields but Issuance does not have "
             "lsfMPTCanHoldConfidentialBalance "
             "set"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                // Inject all three encrypted fields consistently (inbox+spending+issuer must be
                // in sync or badConsistency fires first and masks requiresPrivacyFlag).
                sleToken->setFieldVL(sfConfidentialBalanceInbox, Blob{0x00});
                sleToken->setFieldVL(sfConfidentialBalanceSpending, Blob{0x00});
                sleToken->setFieldVL(sfIssuerEncryptedBalance, Blob{0x00});
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseNoPrivacy);

        // badCOA
        doInvariantCheck(
            {"Confidential outstanding amount exceeds total outstanding amount"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleIssuance = ac.view().peek(keylet::mptokenIssuance(mptID));
                if (!sleIssuance)
                    return false;
                // Total outstanding is natively 100; bloat the COA over 100
                sleIssuance->setFieldU64(sfConfidentialOutstandingAmount, 200);
                ac.view().update(sleIssuance);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_ISSUANCE_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // Conservation Violation
        doInvariantCheck(
            {"Token conservation violation for MPT"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleIssuance = ac.view().peek(keylet::mptokenIssuance(mptID));
                if (!sleIssuance)
                    return false;

                sleIssuance->setFieldU64(
                    sfConfidentialOutstandingAmount,
                    sleIssuance->getFieldU64(sfConfidentialOutstandingAmount) - 10);
                ac.view().update(sleIssuance);

                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // Send/MergeInbox must not change OutstandingAmount (coaDelta == 0)
        doInvariantCheck(
            {"Invariant failed: OutstandingAmount changed "
             "by confidential transaction that should not "
             "modify it for MPT"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleIssuance = ac.view().peek(keylet::mptokenIssuance(mptID));
                if (!sleIssuance)
                    return false;
                sleIssuance->setFieldU64(
                    sfOutstandingAmount, sleIssuance->getFieldU64(sfOutstandingAmount) + 1);
                ac.view().update(sleIssuance);
                return true;
            },
            XRPAmount{},
            STTx{ttCONFIDENTIAL_MPT_SEND, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // Send/MergeInbox and zero-COA-delta confidential transactions must not
        // change public holder MPTAmount.
        doInvariantCheck(
            {"Invariant failed: MPTAmount changed by confidential "
             "transaction that should not modify this field."},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                sleToken->setFieldU64(sfMPTAmount, sleToken->getFieldU64(sfMPTAmount) + 1);
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttCONFIDENTIAL_MPT_SEND, [](STObject&) {}},
            // Second pass is tef: the bumped MPTAmount also trips
            // ValidMPTTransfer's on-failure check, which escalates the tec.
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseConfidential);

        // badVersion
        doInvariantCheck(
            {"MPToken sfConfidentialBalanceVersion not updated when sfConfidentialBalanceSpending "
             "changed"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                Blob const kChangedConfidentialSpending = {0xBA, 0xDD};
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                sleToken->setFieldVL(sfConfidentialBalanceSpending, kChangedConfidentialSpending);

                // DO NOT update sfConfidentialBalanceVersion
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // Skipping Deleted MPTs (Issuance deleted)
        auto const precloseOrphan = [&mptID](
                                        Account const& a1, Account const& a2, Env& env) -> bool {
            MPTTester mpt(env, a1, {.holders = {a2}, .fund = false});
            mpt.create({.flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
            mptID = mpt.issuanceID();
            mpt.authorize({.account = a2});

            // Generate privacy keys and convert 0 amount so Bob has the encrypted fields
            mpt.generateKeyPair(a1);
            mpt.set({.account = a1, .issuerPubKey = mpt.getPubKey(a1)});
            mpt.generateKeyPair(a2);
            mpt.convert({
                .account = a2,
                .amt = 0,
                .holderPubKey = mpt.getPubKey(a2),
            });

            // Immediately destroy the issuance. A2's empty, encrypted token object lives on.
            mpt.destroy();
            return true;
        };

        doInvariantCheck(
            {},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                // Safely able to erase the deleted token.
                ac.view().erase(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tesSUCCESS, tesSUCCESS},
            precloseOrphan);
    }

public:
    void
    run() override
    {
        testXRPNotCreated();
        testAccountRootsNotRemoved();
        testAccountRootsDeletedClean();
        testTypesMatch();
        testNoXRPTrustLine();
        testNoDeepFreezeTrustLinesWithoutFreeze();
        testTransfersNotFrozen();
        testXRPBalanceCheck();
        testTransactionFeeCheck();
        testNoBadOffers();
        testNoZeroEscrow();
        testValidNewAccountRoot();
        testNFTokenPageInvariants();
        testAMMDeleteInvariants(defaultAmendments());
        testAMMDeleteInvariants(defaultAmendments() - fixCleanup3_3_0);
        testPermissionedDomainInvariants(defaultAmendments() | fixCleanup3_1_3);
        testPermissionedDomainInvariants(defaultAmendments() - fixCleanup3_1_3);
        testPermissionedDEX(defaultAmendments() | fixCleanup3_1_3);
        testPermissionedDEX(defaultAmendments() - fixCleanup3_1_3);
        testPermissionedDEXDeletedOfferFallback();
        testBookDirectoryExchangeRate();
        testNoModifiedUnmodifiableFields();
        testValidPseudoAccounts();
        testPseudoAccountLoanBrokerLink();
        testValidLoanBroker();
        testVault();
        testConfidentialMPTTransfer();
        testMPT();
        testInvariantOverwrite(defaultAmendments());
        testInvariantOverwrite(defaultAmendments() - fixCleanup3_1_3);
        testVaultComputeCoarsestScale();
        testAMM();
        testObjectHasPseudoAccount();
        testSponsorship();
        testTxCheckException();
        testTxCheckFinalizeFalse();
    }
};

BEAST_DEFINE_TESTSUITE(Invariants, app, xrpl);

}  // namespace xrpl::test
