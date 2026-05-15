#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/tags.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/AccountID.h>
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
#include <xrpl/tx/invariants/VaultInvariant.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
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

    /** Run a specific test case to put the ledger into a state that will be
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
        TxAccount setTxAccount = TxAccount::None)
    {
        doInvariantCheck(
            test::jtx::Env(*this, defaultAmendments()),
            expectLogs,
            precheck,
            fee,
            tx,
            ters,
            preclose,
            setTxAccount);
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
        TxAccount setTxAccount = TxAccount::None)
    {
        using namespace test::jtx;

        Account const a1{"A1"};
        Account const a2{"A2"};
        env.fund(XRP(1000), a1, a2);
        if (preclose)
            BEAST_EXPECT(preclose(a1, a2, env));
        env.close();

        if (setTxAccount != TxAccount::None)
            tx.setAccountID(sfAccount, setTxAccount == TxAccount::A1 ? a1.id() : a2.id());

        doInvariantCheck(std::move(env), a1, a2, expectLogs, precheck, fee, tx, ters);
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
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED})
    {
        using namespace test::jtx;

        OpenView ov{*env.current()};
        test::StreamSink sink{beast::Severity::Warning};
        beast::Journal const jlog{sink};
        ApplyContext ac{env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};

        // Invariants normally run in the Transaction's "apply" (operator()) context, and can always
        // access global Rules.
        CurrentTransactionRulesGuard const rg(ov.rules());

        BEAST_EXPECT(precheck(a1, a2, ac));

        auto transactor = makeTransactor(ac);
        if (!BEAST_EXPECT(transactor))
            return;

        // invoke check twice to cover tec and tef cases
        if (!BEAST_EXPECT(ters.size() == 2))
            return;

        TER terActual = tesSUCCESS;
        for (TER const& terExpect : ters)
        {
            terActual = transactor->checkInvariants(terActual, fee);
            BEAST_EXPECTS(
                terExpect == terActual,
                "expected: " + transToken(terExpect) + " got: " + transToken(terActual));
            auto const messages = sink.messages().str();

            if (!isTesSuccess(terActual))
            {
                BEAST_EXPECTS(
                    messages.starts_with("Invariant failed:") ||
                        messages.starts_with("Transaction caused an exception"),
                    messages);
            }

            // std::cerr << messages << '\n';
            for (auto const& m : expectLogs)
            {
                BEAST_EXPECTS(messages.find(m) != std::string::npos, m);
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
                adjustOwnerCount(ac.view(), sleA1, 1, ac.journal);

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        for (auto const& keyletInfo : kDirectAccountKeylets)
        {
            // TODO: Use structured binding once LLVM 16 is the minimum
            // supported version. See also:
            // https://github.com/llvm/llvm-project/issues/48582
            // https://github.com/llvm/llvm-project/commit/127bf44385424891eb04cff8e52d3f157fc2cb7c
            if (!keyletInfo.includeInTests)
                continue;
            auto const& keyletfunc = keyletInfo.function;
            auto const& type = keyletInfo.expectedLEName;

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
                     {keylet::line(ammAcctID, a1["USD"]), keylet::line(a1, ammIssue)})
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
                auto const sleNew =
                    std::make_shared<SLE>(keylet::escrow(a1, (*sle)[sfSequence] + 2));

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
                    std::make_shared<SLE>(keylet::line(a1, a2, xrpIssue().currency));
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
                auto const sleNew = std::make_shared<SLE>(keylet::line(a1, a2, a1["USD"].currency));
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
                auto const sleNew = std::make_shared<SLE>(keylet::line(a1, a2, a1["USD"].currency));
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
                auto const sleNew = std::make_shared<SLE>(keylet::line(a1, a2, a1["USD"].currency));
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
                auto const sleNew = std::make_shared<SLE>(keylet::line(a1, a2, a1["USD"].currency));
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
                auto const sleNew = std::make_shared<SLE>(keylet::line(a1, a2, a1["USD"].currency));
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
            auto const sleA1 = ac.view().peek(keylet::line(a1, g1["USD"]));
            auto const sleA2 = ac.view().peek(keylet::line(a2, g1["USD"]));

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
                auto sleNew = std::make_shared<SLE>(keylet::offer(a1.id(), (*sle)[sfSequence]));
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
                auto sleNew = std::make_shared<SLE>(keylet::offer(a1.id(), (*sle)[sfSequence]));
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
                auto sleNew = std::make_shared<SLE>(keylet::offer(a1.id(), (*sle)[sfSequence]));
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
                auto sleNew = std::make_shared<SLE>(keylet::escrow(a1, (*sle)[sfSequence] + 2));
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
                auto sleNew = std::make_shared<SLE>(keylet::escrow(a1, (*sle)[sfSequence] + 2));
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
                auto sleNew = std::make_shared<SLE>(keylet::escrow(a1, (*sle)[sfSequence] + 2));

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
                auto sleNew = std::make_shared<SLE>(keylet::escrow(a1, (*sle)[sfSequence] + 2));

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
                auto sleNew = std::make_shared<SLE>(keylet::escrow(a1, (*sle)[sfSequence] + 2));

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
                auto sleNew = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
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
                auto sleNew = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
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
                auto sleNew = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
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
                auto nftPage = std::make_shared<SLE>(keylet::nftpageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(0));

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page has invalid size"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(33));

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFTs on page are not sorted"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(2);
                std::iter_swap(nfTokens.begin(), nfTokens.begin() + 1);

                auto nftPage = std::make_shared<SLE>(keylet::nftpageMax(a1));
                nftPage->setFieldArray(sfNFTokens, nfTokens);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT contains empty URI"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(1);
                nfTokens[0].setFieldVL(sfURI, Blob{});

                auto nftPage = std::make_shared<SLE>(keylet::nftpageMax(a1));
                nftPage->setFieldArray(sfNFTokens, nfTokens);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(sfPreviousPageMin, keylet::nftpageMax(a1).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(sfPreviousPageMin, keylet::nftpageMin(a2).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpageMax(a1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(sfNextPageMin, nftPage->key());

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](Account const& a1, Account const& a2, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(1);
                auto nftPage = std::make_shared<SLE>(keylet::nftpage(
                    keylet::nftpageMax(a1), ++(nfTokens[0].getFieldH256(sfNFTokenID))));
                nftPage->setFieldArray(sfNFTokens, nfTokens);
                nftPage->setFieldH256(sfNextPageMin, keylet::nftpageMax(a2).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT found in incorrect page"}},
            [&makeNFTokenIDs](Account const& a1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(2);
                auto nftPage = std::make_shared<SLE>(keylet::nftpage(
                    keylet::nftpageMax(a1), (nfTokens[1].getFieldH256(sfNFTokenID))));
                nftPage->setFieldArray(sfNFTokens, nfTokens);

                ac.view().insert(nftPage);
                return true;
            });
    }

    static std::shared_ptr<SLE>
    createPermissionedDomain(
        ApplyContext& ac,
        test::jtx::Account const& a1,
        test::jtx::Account const& a2,
        std::uint32_t numCreds = 2,
        std::uint32_t seq = 10)
    {
        Keylet const pdKeylet = keylet::permissionedDomain(a1.id(), seq);
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
            Env(*this, features),
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
            Env(*this, features),
            {{"permissioned domain bad credentials size " + std::to_string(kTooBig)}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                return !!createPermissionedDomain(ac, a1, a2, kTooBig);
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain 3";
        doInvariantCheck(
            Env(*this, features),
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
            Env(*this, features),
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
            Env(*this, features),
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
            Env(*this, features),
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
            Env(*this, features),
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
            Env(*this, features),
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
                Env(*this, features),
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
                Env(*this, features),
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
                Env(*this, features),
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
                Env(*this, features),
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
            credentials.push_back({a2, credType});
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
            Env(*this, features),
            {{"domain doesn't exist"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                Keylet const offerKey = keylet::offer(a1.id(), 10);
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
            Env(*this, features),
            {{"hybrid offer is malformed"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                Keylet const offerKey = keylet::offer(a2.id(), 10);
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
                    Keylet const offerKey = keylet::offer(a2.id(), 10);
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
                    Keylet const offerKey = keylet::offer(a2.id(), 10);
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
                    Keylet const offerKey = keylet::offer(a2.id(), 10);
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
                    Keylet const offerKey = keylet::offer(a2.id(), 10);
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
                    Keylet const offerKey = keylet::offer(a2.id(), 10);
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
        using namespace loanBroker;

        auto const loanBrokerKeylet = keylet::loanbroker(a.id(), env.seq(a));
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

        // TODO: Loan Object

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
                        env(loanBroker::coverDeposit(alice, brokerKeylet.key, asset(10)));
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
    testVault()
    {
        using namespace test::jtx;

        struct AccountAmount
        {
            AccountID account;
            int amount;
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
            // NOLINTEND(readability-redundant-member-init)
        };
        constexpr auto kAdjust = [&](ApplyView& ac, xrpl::Keylet keylet, Adjustments args) {
            auto sleVault = ac.peek(keylet);
            if (!sleVault)
                return false;

            auto const mptIssuanceID = (*sleVault)[sfShareMPTID];
            auto sleShares = ac.peek(keylet::mptIssuance(mptIssuanceID));
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            {"vault updated by a wrong transaction type"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const vaultKeylet = keylet::vault(a1.id(), sequence);
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);
                ac.view().insert(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {"vault deleted by a wrong transaction type"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation updated more than single vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                {
                    auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                    auto sleVault = ac.view().peek(keylet);
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);
                }
                {
                    auto const keylet = keylet::vault(a2.id(), ac.view().seq());
                    auto sleVault = ac.view().peek(keylet);
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);
                }
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
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
                    auto const vaultKeylet = keylet::vault(a.id(), sequence);
                    auto sleVault = std::make_shared<SLE>(vaultKeylet);
                    auto const vaultPage = ac.view().dirInsert(
                        keylet::ownerDir(a.id()), sleVault->key(), describeOwnerDir(a.id()));
                    sleVault->setFieldU64(sfOwnerNode, *vaultPage);
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
            {"deleted vault must also delete shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
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
            {"deleted vault must have no shares outstanding",
             "deleted vault must have no assets outstanding",
             "deleted vault must have no assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*sleVault)[sfShareMPTID]));
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*sleVault)[sfShareMPTID]));
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsMaximum] = 200;
                ac.view().update(sleVault);

                auto sleShares = ac.view().peek(keylet::mptIssuance((*sleVault)[sfShareMPTID]));
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
             "assets available must be positive",
             "assets available must not be greater than assets outstanding",
             "assets outstanding must be positive"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 100, [&](Adjustments& sample) {
                                   sample.lossUnrealized = 13;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"set assets outstanding must not exceed assets maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            {"assets maximum must be positive"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                auto sleShares = ac.view().peek(keylet::mptIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfOutstandingAmount] = 0;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfMaximumAmount] = 10;
                ac.view().update(sleShares);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments&) {}));

                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*sleVault)[sfShareMPTID]));
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

        testcase << "Vault create";
        doInvariantCheck(
            {
                "created vault must be empty",
                "updated zero sized vault must have no assets outstanding",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().update(sleVault);
                (*sleShares)[sfOutstandingAmount] = 9;
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
            {
                "assets maximum must be positive",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*sleVault)[sfShareMPTID]));
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
                auto const vaultKeylet = keylet::vault(a1.id(), sequence);
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
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
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
                auto const vaultKeylet = keylet::vault(a1.id(), sequence);
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
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
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
                auto const vaultKeylet = keylet::vault(a1.id(), sequence);
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto const sharesMptId = makeMptID(sequence, a2.id());
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 200, [&](Adjustments& sample) {
                                   sample.assetsMaximum = 1;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // This really convoluted unit tests makes the zero balance on the
        // depositor, by sending them the same amount as the transaction fee.
        // The operation makes no sense, but the defensive check in
        // ValidVault::finalize is otherwise impossible to trigger.
        doInvariantCheck(
            {"deposit must increase vault balance", "deposit must change depositor balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());

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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"deposit must increase vault balance",
             "deposit must decrease depositor balance",
             "deposit must change vault and depositor balance by equal amount",
             "deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());

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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change depositor balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());

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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change depositor shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.accountShares.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change vault shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments& sample) {
                                   sample.sharesTotal = 0;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must increase depositor shares",
             "deposit must change depositor and vault shares by equal amount",
             "deposit must not change vault balance by more than deposited "
             "amount"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = -5;
                                   sample.sharesTotal = -10;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(5); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit and assets outstanding must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 2000;
                ac.view().update(sleA3);

                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.assetsTotal = 7;
                                   sample.assetsAvailable = 7;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        testcase << "Vault withdrawal";
        doInvariantCheck(
            {"withdrawal must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());

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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());

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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change one destination balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change depositor shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change vault shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [](Adjustments& sample) {
                                   sample.sharesTotal = 0;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must decrease depositor shares",
             "withdrawal must change depositor and vault shares by equal "
             "amount"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = 5;
                                   sample.sharesTotal = 10;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal and assets outstanding must add up",
             "withdrawal and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.assetsTotal = -15;
                                   sample.assetsAvailable = -15;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal and assets outstanding must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 2000;
                ac.view().update(sleA3);

                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq() - 2);
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = 5;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [&](STObject& tx) { tx[sfAccount] = a3.id(); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt,
            TxAccount::A2);

        testcase << "Vault clawback";
        doInvariantCheck(
            {"clawback must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq() - 2);
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -1, [&](Adjustments& sample) {
                                   sample.vaultAssets.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [&](STObject& tx) { tx[sfAccount] = a3.id(); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        // Not the same as below check: attempt to clawback XRP
        doInvariantCheck(
            {"clawback may only be performed by the asset issuer"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq());
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq() - 2);
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
                auto const keylet = keylet::vault(a1.id(), ac.view().seq() - 2);
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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must change holder shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq() - 2);
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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must change holder and vault shares by equal amount",
             "clawback and assets outstanding must add up",
             "clawback and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), ac.view().seq() - 2);
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
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);
    }

    void
    testMPT()
    {
        using namespace test::jtx;
        testcase << "MPT";

        // MPT OutstandingAmount > MaximumAmount
        doInvariantCheck(
            {{"OutstandingAmount overflow"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // mptissuance outstanding is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
                auto sleNew = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
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
                auto sleNew = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
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
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
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
                auto sle = ac.view().peek(keylet::mptIssuance(id));
                if (!sle)
                    return false;
                sle->setFieldU64(sfOutstandingAmount, 101);
                ac.view().update(sle);
                return true;
            });

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
                        auto sleNew = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
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
            auto const k1 = keylet::line(a1, a2, a1[c1].currency);
            auto const k2 = keylet::line(a1, a3, a1[c2].currency);

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
            Env(*this, features),
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
            Env(*this, features),
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
            Env(*this, features),
            fixEnabled ? std::vector<std::string>{{"escrow specifies invalid amount"}}
                       : std::vector<std::string>{{"a MPT issuance was created"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(1, AccountID(0x4985601))};
                auto sleNew = std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
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

        NumberMantissaScaleGuard const g{MantissaRange::MantissaScale::Large};

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
                    {makeDelta(Number{1, -2}), makeDelta(Number{5, -3}), makeDelta(Number{3, -2})},
            },
            {
                .name = "Equal scales",
                .expectedMinScale = -16,
                .values =
                    {makeDelta(Number{1, -1}), makeDelta(Number{5, -1}), makeDelta(Number{1, -1})},
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
                    "at a scale of " + std::to_string(actualScale) + " " + to_string(num.delta) +
                        " == " + to_string(*first));
            }
        }
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
        testPermissionedDomainInvariants(defaultAmendments() | fixCleanup3_1_3);
        testPermissionedDomainInvariants(defaultAmendments() - fixCleanup3_1_3);
        testPermissionedDEX(defaultAmendments() | fixCleanup3_1_3);
        testPermissionedDEX(defaultAmendments() - fixCleanup3_1_3);
        testNoModifiedUnmodifiableFields();
        testValidPseudoAccounts();
        testValidLoanBroker();
        testVault();
        testMPT();
        testInvariantOverwrite(defaultAmendments());
        testInvariantOverwrite(defaultAmendments() - fixCleanup3_1_3);
        testVaultComputeCoarsestScale();
    }
};

BEAST_DEFINE_TESTSUITE(Invariants, app, xrpl);

}  // namespace xrpl::test
