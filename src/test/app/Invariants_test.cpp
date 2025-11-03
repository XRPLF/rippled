//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Env.h>

#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/ApplyContext.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <boost/algorithm/string/predicate.hpp>

namespace ripple {
namespace test {

class Invariants_test : public beast::unit_test::suite
{
    // The optional Preclose function is used to process additional transactions
    // on the ledger after creating two accounts, but before closing it, and
    // before the Precheck function. These should only be valid functions, and
    // not direct manipulations. Preclose is not commonly used.
    using Preclose = std::function<bool(
        test::jtx::Account const& a,
        test::jtx::Account const& b,
        test::jtx::Env& env)>;

    // this is common setup/method for running a failing invariant check. The
    // precheck function is used to manipulate the ApplyContext with view
    // changes that will cause the check to fail.
    using Precheck = std::function<bool(
        test::jtx::Account const& a,
        test::jtx::Account const& b,
        ApplyContext& ac)>;

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
        std::vector<std::string> const& expect_logs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        std::initializer_list<TER> ters =
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        Preclose const& preclose = {},
        TxAccount setTxAccount = TxAccount::None)
    {
        using namespace test::jtx;
        FeatureBitset amendments = testable_amendments() |
            featureInvariantsV1_1 | featureSingleAssetVault;
        Env env{*this, amendments};

        Account const A1{"A1"};
        Account const A2{"A2"};
        env.fund(XRP(1000), A1, A2);
        if (preclose)
            BEAST_EXPECT(preclose(A1, A2, env));
        env.close();

        OpenView ov{*env.current()};
        test::StreamSink sink{beast::severities::kWarning};
        beast::Journal jlog{sink};
        if (setTxAccount != TxAccount::None)
            tx.setAccountID(
                sfAccount, setTxAccount == TxAccount::A1 ? A1.id() : A2.id());
        ApplyContext ac{
            env.app(),
            ov,
            tx,
            tesSUCCESS,
            env.current()->fees().base,
            tapNONE,
            jlog};

        BEAST_EXPECT(precheck(A1, A2, ac));

        // invoke check twice to cover tec and tef cases
        if (!BEAST_EXPECT(ters.size() == 2))
            return;

        TER terActual = tesSUCCESS;
        for (TER const& terExpect : ters)
        {
            terActual = ac.checkInvariants(terActual, fee);
            BEAST_EXPECT(terExpect == terActual);
            auto const messages = sink.messages().str();
            BEAST_EXPECT(
                messages.starts_with("Invariant failed:") ||
                messages.starts_with("Transaction caused an exception"));
            // std::cerr << messages << '\n';
            for (auto const& m : expect_logs)
            {
                if (messages.find(m) == std::string::npos)
                {
                    // uncomment if you want to log the invariant failure
                    // std::cerr << "   --> " << m << std::endl;
                    fail();
                }
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
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // put a single account in the view and "manufacture" some XRP
                auto const sle = ac.view().peek(keylet::account(A1.id()));
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
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // remove an account from the view
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
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
            [](Account const&, Account const&, ApplyContext& ac) {
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // Successful AccountDelete that deleted more than one account.
        doInvariantCheck(
            {{"account deletion succeeded but deleted multiple accounts"}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                // remove two accounts from the view
                auto const sleA1 = ac.view().peek(keylet::account(A1.id()));
                auto const sleA2 = ac.view().peek(keylet::account(A2.id()));
                if (!sleA1 || !sleA2)
                    return false;
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

        for (auto const& keyletInfo : directAccountKeylets)
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
                {{"account deletion left behind a "s + type.c_str() +
                  " object"}},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                    // Add an object to the ledger for account A1, then delete
                    // A1
                    auto const a1 = A1.id();
                    auto const sleA1 = ac.view().peek(keylet::account(a1));
                    if (!sleA1)
                        return false;

                    auto const key = std::invoke(keyletfunc, a1);
                    auto const newSLE = std::make_shared<SLE>(key);
                    ac.view().insert(newSLE);
                    ac.view().erase(sleA1);

                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});
        };

        // NFT special case
        doInvariantCheck(
            {{"account deletion left behind a NFTokenPage object"}},
            [&](Account const& A1, Account const&, ApplyContext& ac) {
                // remove an account from the view
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& A1, Account const&, Env& env) {
                // Preclose callback to mint the NFT which will be deleted in
                // the Precheck callback above.
                env(token::mint(A1));

                return true;
            });

        // AMM special cases
        AccountID ammAcctID;
        uint256 ammKey;
        Issue ammIssue;
        doInvariantCheck(
            {{"account deletion left behind a DirectoryNode object"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                // Delete the AMM account without cleaning up the directory or
                // deleting the AMM object
                auto const sle = ac.view().peek(keylet::account(ammAcctID));
                if (!sle)
                    return false;

                BEAST_EXPECT(sle->at(~sfAMMID));
                BEAST_EXPECT(sle->at(~sfAMMID) == ammKey);

                ac.view().erase(sle);

                return true;
            },
            XRPAmount{},
            STTx{ttAMM_WITHDRAW, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                // Preclose callback to create the AMM which will be partially
                // deleted in the Precheck callback above.
                AMM const amm(env, A1, XRP(100), A1["USD"](50));
                ammAcctID = amm.ammAccount();
                ammKey = amm.ammID();
                ammIssue = amm.lptIssue();
                return true;
            });
        doInvariantCheck(
            {{"account deletion left behind a AMM object"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                // Delete all the AMM's trust lines, remove the AMM from the AMM
                // account's directory (this deletes the directory), and delete
                // the AMM account. Do not delete the AMM object.
                auto const sle = ac.view().peek(keylet::account(ammAcctID));
                if (!sle)
                    return false;

                BEAST_EXPECT(sle->at(~sfAMMID));
                BEAST_EXPECT(sle->at(~sfAMMID) == ammKey);

                for (auto const& trustKeylet :
                     {keylet::line(ammAcctID, A1["USD"]),
                      keylet::line(A1, ammIssue)})
                {
                    if (auto const line = ac.view().peek(trustKeylet); !line)
                    {
                        return false;
                    }
                    else
                    {
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
                }

                auto const ammSle = ac.view().peek(keylet::amm(ammKey));
                if (!BEAST_EXPECT(ammSle))
                    return false;
                auto const ownerDirKeylet = keylet::ownerDir(ammAcctID);

                BEAST_EXPECT(ac.view().dirRemove(
                    ownerDirKeylet, ammSle->at(sfOwnerNode), ammKey, false));
                BEAST_EXPECT(
                    !ac.view().exists(ownerDirKeylet) ||
                    ac.view().emptyDirDelete(ownerDirKeylet));

                ac.view().erase(sle);

                return true;
            },
            XRPAmount{},
            STTx{ttAMM_WITHDRAW, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                // Preclose callback to create the AMM which will be partially
                // deleted in the Precheck callback above.
                AMM const amm(env, A1, XRP(100), A1["USD"](50));
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
            {{"ledger entry type mismatch"},
             {"XRP net change of -1000000000 doesn't match fee 0"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // replace an entry in the table with an SLE of a different type
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto const sleNew = std::make_shared<SLE>(ltTICKET, sle->key());
                ac.rawView().rawReplace(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"invalid ledger entry type added"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // add an entry in the table with an SLE of an invalid type
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;

                // make a dummy escrow ledger entry, then change the type to an
                // unsupported value so that the valid type invariant check
                // will fail.
                auto const sleNew = std::make_shared<SLE>(
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));

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
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                // create simple trust SLE with xrp currency
                auto const sleNew = std::make_shared<SLE>(
                    keylet::line(A1, A2, xrpIssue().currency));
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
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sleNew = std::make_shared<SLE>(
                    keylet::line(A1, A2, A1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, A1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, A1["USD"](0));

                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sleNew = std::make_shared<SLE>(
                    keylet::line(A1, A2, A1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, A1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, A1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sleNew = std::make_shared<SLE>(
                    keylet::line(A1, A2, A1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, A1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, A1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze | lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sleNew = std::make_shared<SLE>(
                    keylet::line(A1, A2, A1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, A1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, A1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze | lsfHighFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sleNew = std::make_shared<SLE>(
                    keylet::line(A1, A2, A1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, A1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, A1["USD"](0));
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

        Account G1{"G1"};
        // Helper function to establish the trustlines
        auto const createTrustlines =
            [&](Account const& A1, Account const& A2, Env& env) {
                // Preclose callback to establish trust lines with gateway
                env.fund(XRP(1000), G1);

                env.trust(G1["USD"](10000), A1);
                env.trust(G1["USD"](10000), A2);
                env.close();

                env(pay(G1, A1, G1["USD"](1000)));
                env(pay(G1, A2, G1["USD"](1000)));
                env.close();

                return true;
            };

        auto const A1FrozenByIssuer =
            [&](Account const& A1, Account const& A2, Env& env) {
                createTrustlines(A1, A2, env);
                env(trust(G1, A1["USD"](10000), tfSetFreeze));
                env.close();

                return true;
            };

        auto const A1DeepFrozenByIssuer =
            [&](Account const& A1, Account const& A2, Env& env) {
                A1FrozenByIssuer(A1, A2, env);
                env(trust(G1, A1["USD"](10000), tfSetDeepFreeze));
                env.close();

                return true;
            };

        auto const changeBalances = [&](Account const& A1,
                                        Account const& A2,
                                        ApplyContext& ac,
                                        int A1Balance,
                                        int A2Balance) {
            auto const sleA1 = ac.view().peek(keylet::line(A1, G1["USD"]));
            auto const sleA2 = ac.view().peek(keylet::line(A2, G1["USD"]));

            sleA1->setFieldAmount(sfBalance, G1["USD"](A1Balance));
            sleA2->setFieldAmount(sfBalance, G1["USD"](A2Balance));

            ac.view().update(sleA1);
            ac.view().update(sleA2);
        };

        // test: imitating frozen A1 making a payment to A2.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                changeBalances(A1, A2, ac, -900, -1100);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            A1FrozenByIssuer);

        // test: imitating deep frozen A1 making a payment to A2.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                changeBalances(A1, A2, ac, -900, -1100);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            A1DeepFrozenByIssuer);

        // test: imitating A2 making a payment to deep frozen A1.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                changeBalances(A1, A2, ac, -1100, -900);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            A1DeepFrozenByIssuer);
    }

    void
    testXRPBalanceCheck()
    {
        using namespace test::jtx;
        testcase << "XRP balance checks";

        doInvariantCheck(
            {{"Cannot return non-native STAmount as XRPAmount"}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                // non-native balance
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                STAmount const nonNative(A2["USD"](51));
                sle->setFieldAmount(sfBalance, nonNative);
                ac.view().update(sle);
                return true;
            });

        doInvariantCheck(
            {{"incorrect account XRP balance"},
             {"XRP net change was positive: 99999999000000001"}},
            [this](Account const& A1, Account const&, ApplyContext& ac) {
                // balance exceeds genesis amount
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                // Use `drops(1)` to bypass a call to STAmount::canonicalize
                // with an invalid value
                sle->setFieldAmount(sfBalance, INITIAL_XRP + drops(1));
                BEAST_EXPECT(!sle->getFieldAmount(sfBalance).negative());
                ac.view().update(sle);
                return true;
            });

        doInvariantCheck(
            {{"incorrect account XRP balance"},
             {"XRP net change of -1000000001 doesn't match fee 0"}},
            [this](Account const& A1, Account const&, ApplyContext& ac) {
                // balance is negative
                auto const sle = ac.view().peek(keylet::account(A1.id()));
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
            {{"fee paid was negative: -1"},
             {"XRP net change of 0 doesn't match fee -1"}},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{-1});

        doInvariantCheck(
            {{"fee paid exceeds system limit: "s + to_string(INITIAL_XRP)},
             {"XRP net change of 0 doesn't match fee "s +
              to_string(INITIAL_XRP)}},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{INITIAL_XRP});

        doInvariantCheck(
            {{"fee paid is 20 exceeds fee specified in transaction."},
             {"XRP net change of 0 doesn't match fee 20"}},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{20},
            STTx{ttACCOUNT_SET, [](STObject& tx) {
                     tx.setFieldAmount(sfFee, XRPAmount{10});
                 }});
    }

    void
    testNoBadOffers()
    {
        using namespace test::jtx;
        testcase << "no bad offers";

        doInvariantCheck(
            {{"offer with a bad amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // offer with negative takerpays
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::offer(A1.id(), (*sle)[sfSequence]));
                sleNew->setAccountID(sfAccount, A1.id());
                sleNew->setFieldU32(sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount(sfTakerPays, XRP(-1));
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"offer with a bad amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // offer with negative takergets
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::offer(A1.id(), (*sle)[sfSequence]));
                sleNew->setAccountID(sfAccount, A1.id());
                sleNew->setFieldU32(sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount(sfTakerPays, A1["USD"](10));
                sleNew->setFieldAmount(sfTakerGets, XRP(-1));
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"offer with a bad amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // offer XRP to XRP
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::offer(A1.id(), (*sle)[sfSequence]));
                sleNew->setAccountID(sfAccount, A1.id());
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
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // escrow with negative amount
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                sleNew->setFieldAmount(sfAmount, XRP(-1));
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"XRP net change was positive: 100000000000000001"},
             {"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // escrow with too-large amount
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                // Use `drops(1)` to bypass a call to STAmount::canonicalize
                // with an invalid value
                sleNew->setFieldAmount(sfAmount, INITIAL_XRP + drops(1));
                ac.view().insert(sleNew);
                return true;
            });

        // IOU < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // escrow with too-little iou
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));

                Issue const usd{
                    Currency(0x5553440000000000), AccountID(0x4985601)};
                STAmount amt(usd, -1);
                sleNew->setFieldAmount(sfAmount, amt);
                ac.view().insert(sleNew);
                return true;
            });

        // IOU bad currency
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // escrow with bad iou currency
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));

                Issue const bad{badCurrency(), AccountID(0x4985601)};
                STAmount amt(bad, 1);
                sleNew->setFieldAmount(sfAmount, amt);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // escrow with too-little mpt
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                auto sleNew = std::make_shared<SLE>(
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));

                MPTIssue const mpt{
                    MPTIssue{makeMptID(1, AccountID(0x4985601))}};
                STAmount amt(mpt, -1);
                sleNew->setFieldAmount(sfAmount, amt);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT OutstandingAmount < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // mpissuance outstanding is negative
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{
                    MPTIssue{makeMptID(1, AccountID(0x4985601))}};
                auto sleNew =
                    std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfOutstandingAmount, -1);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT LockedAmount < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // mpissuance locked is less than locked
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{
                    MPTIssue{makeMptID(1, AccountID(0x4985601))}};
                auto sleNew =
                    std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfLockedAmount, -1);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT OutstandingAmount < LockedAmount
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // mpissuance outstanding is less than locked
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{
                    MPTIssue{makeMptID(1, AccountID(0x4985601))}};
                auto sleNew =
                    std::make_shared<SLE>(keylet::mptIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfOutstandingAmount, 1);
                sleNew->setFieldU64(sfLockedAmount, 10);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT MPTAmount < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // mptoken amount is negative
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{
                    MPTIssue{makeMptID(1, AccountID(0x4985601))}};
                auto sleNew =
                    std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), A1));
                sleNew->setFieldU64(sfMPTAmount, -1);
                ac.view().insert(sleNew);
                return true;
            });

        // MPT LockedAmount < 0
        doInvariantCheck(
            {{"escrow specifies invalid amount"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                // mptoken locked amount is negative
                auto const sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{
                    MPTIssue{makeMptID(1, AccountID(0x4985601))}};
                auto sleNew =
                    std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), A1));
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
                Account const A3{"A3"};
                Keylet const acctKeylet = keylet::account(A3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"multiple accounts created in a single transaction"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                // Insert two new account roots into the view.
                {
                    Account const A3{"A3"};
                    Keylet const acctKeylet = keylet::account(A3);
                    auto const sleA3 = std::make_shared<SLE>(acctKeylet);
                    ac.view().insert(sleA3);
                }
                {
                    Account const A4{"A4"};
                    Keylet const acctKeylet = keylet::account(A4);
                    auto const sleA4 = std::make_shared<SLE>(acctKeylet);
                    ac.view().insert(sleA4);
                }
                return true;
            });

        doInvariantCheck(
            {{"account created with wrong starting sequence number"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                // Insert a new account root with the wrong starting sequence.
                Account const A3{"A3"};
                Keylet const acctKeylet = keylet::account(A3);
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
                Account const A3{"A3"};
                Keylet const acctKeylet = keylet::account(A3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, 0);
                sleNew->setFieldH256(sfAMMID, uint256(1));
                sleNew->setFieldU32(
                    sfFlags,
                    lsfDisableMaster | lsfDefaultRipple | lsfDefaultRipple);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}});

        doInvariantCheck(
            {{"account created with wrong starting sequence number"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                Account const A3{"A3"};
                Keylet const acctKeylet = keylet::account(A3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, ac.view().seq());
                sleNew->setFieldH256(sfAMMID, uint256(1));
                sleNew->setFieldU32(
                    sfFlags,
                    lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttAMM_CREATE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"pseudo-account created with wrong flags"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                Account const A3{"A3"};
                Keylet const acctKeylet = keylet::account(A3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, 0);
                sleNew->setFieldH256(sfAMMID, uint256(1));
                sleNew->setFieldU32(
                    sfFlags, lsfDisableMaster | lsfDefaultRipple);
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"pseudo-account created with wrong flags"}},
            [](Account const&, Account const&, ApplyContext& ac) {
                Account const A3{"A3"};
                Keylet const acctKeylet = keylet::account(A3);
                auto const sleNew = std::make_shared<SLE>(acctKeylet);
                sleNew->setFieldU32(sfSequence, 0);
                sleNew->setFieldH256(sfAMMID, uint256(1));
                sleNew->setFieldU32(
                    sfFlags,
                    lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth |
                        lsfRequireDestTag);
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
                InnerObjectFormats::getInstance().findSOTemplateBySField(
                    sfNFToken);

            uint256 nftID(firstNFTID);
            STArray ret;
            for (int i = 0; i < nftCount; ++i)
            {
                STObject newNFToken(
                    *nfTokenTemplate, sfNFToken, [&nftID](STObject& object) {
                        object.setFieldH256(sfNFTokenID, nftID);
                    });
                ret.push_back(std::move(newNFToken));
                ++nftID;
            }
            return ret;
        };

        doInvariantCheck(
            {{"NFT page has invalid size"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpage_max(A1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(0));

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page has invalid size"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpage_max(A1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(33));

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFTs on page are not sorted"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(2);
                std::iter_swap(nfTokens.begin(), nfTokens.begin() + 1);

                auto nftPage = std::make_shared<SLE>(keylet::nftpage_max(A1));
                nftPage->setFieldArray(sfNFTokens, nfTokens);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT contains empty URI"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(1);
                nfTokens[0].setFieldVL(sfURI, Blob{});

                auto nftPage = std::make_shared<SLE>(keylet::nftpage_max(A1));
                nftPage->setFieldArray(sfNFTokens, nfTokens);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpage_max(A1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(
                    sfPreviousPageMin, keylet::nftpage_max(A1).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const& A2, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpage_max(A1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(
                    sfPreviousPageMin, keylet::nftpage_min(A2).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const&, ApplyContext& ac) {
                auto nftPage = std::make_shared<SLE>(keylet::nftpage_max(A1));
                nftPage->setFieldArray(sfNFTokens, makeNFTokenIDs(1));
                nftPage->setFieldH256(sfNextPageMin, nftPage->key());

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT page is improperly linked"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const& A2, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(1);
                auto nftPage = std::make_shared<SLE>(keylet::nftpage(
                    keylet::nftpage_max(A1),
                    ++(nfTokens[0].getFieldH256(sfNFTokenID))));
                nftPage->setFieldArray(sfNFTokens, std::move(nfTokens));
                nftPage->setFieldH256(
                    sfNextPageMin, keylet::nftpage_max(A2).key);

                ac.view().insert(nftPage);
                return true;
            });

        doInvariantCheck(
            {{"NFT found in incorrect page"}},
            [&makeNFTokenIDs](
                Account const& A1, Account const&, ApplyContext& ac) {
                STArray nfTokens = makeNFTokenIDs(2);
                auto nftPage = std::make_shared<SLE>(keylet::nftpage(
                    keylet::nftpage_max(A1),
                    (nfTokens[1].getFieldH256(sfNFTokenID))));
                nftPage->setFieldArray(sfNFTokens, std::move(nfTokens));

                ac.view().insert(nftPage);
                return true;
            });
    }

    void
    createPermissionedDomain(
        ApplyContext& ac,
        std::shared_ptr<SLE>& sle,
        test::jtx::Account const& A1,
        test::jtx::Account const& A2)
    {
        sle->setAccountID(sfOwner, A1);
        sle->setFieldU32(sfSequence, 10);

        STArray credentials(sfAcceptedCredentials, 2);
        for (std::size_t n = 0; n < 2; ++n)
        {
            auto cred = STObject::makeInnerObject(sfCredential);
            cred.setAccountID(sfIssuer, A2);
            auto credType = "cred_type" + std::to_string(n);
            cred.setFieldVL(
                sfCredentialType, Slice(credType.c_str(), credType.size()));
            credentials.push_back(std::move(cred));
        }
        sle->setFieldArray(sfAcceptedCredentials, credentials);
        ac.view().insert(sle);
    };

    void
    testPermissionedDomainInvariants()
    {
        using namespace test::jtx;

        testcase << "PermissionedDomain";
        doInvariantCheck(
            {{"permissioned domain with no rules."}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                slePd->setAccountID(sfOwner, A1);
                slePd->setFieldU32(sfSequence, 10);

                ac.view().insert(slePd);
                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase << "PermissionedDomain 2";

        auto constexpr tooBig = maxPermissionedDomainCredentialsArraySize + 1;
        doInvariantCheck(
            {{"permissioned domain bad credentials size " +
              std::to_string(tooBig)}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                slePd->setAccountID(sfOwner, A1);
                slePd->setFieldU32(sfSequence, 10);

                STArray credentials(sfAcceptedCredentials, tooBig);
                for (std::size_t n = 0; n < tooBig; ++n)
                {
                    auto cred = STObject::makeInnerObject(sfCredential);
                    cred.setAccountID(sfIssuer, A2);
                    auto credType =
                        std::string("cred_type") + std::to_string(n);
                    cred.setFieldVL(
                        sfCredentialType,
                        Slice(credType.c_str(), credType.size()));
                    credentials.push_back(std::move(cred));
                }
                slePd->setFieldArray(sfAcceptedCredentials, credentials);
                ac.view().insert(slePd);
                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase << "PermissionedDomain 3";
        doInvariantCheck(
            {{"permissioned domain credentials aren't sorted"}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                slePd->setAccountID(sfOwner, A1);
                slePd->setFieldU32(sfSequence, 10);

                STArray credentials(sfAcceptedCredentials, 2);
                for (std::size_t n = 0; n < 2; ++n)
                {
                    auto cred = STObject::makeInnerObject(sfCredential);
                    cred.setAccountID(sfIssuer, A2);
                    auto credType =
                        std::string("cred_type") + std::to_string(9 - n);
                    cred.setFieldVL(
                        sfCredentialType,
                        Slice(credType.c_str(), credType.size()));
                    credentials.push_back(std::move(cred));
                }
                slePd->setFieldArray(sfAcceptedCredentials, credentials);
                ac.view().insert(slePd);
                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase << "PermissionedDomain 4";
        doInvariantCheck(
            {{"permissioned domain credentials aren't unique"}},
            [](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                slePd->setAccountID(sfOwner, A1);
                slePd->setFieldU32(sfSequence, 10);

                STArray credentials(sfAcceptedCredentials, 2);
                for (std::size_t n = 0; n < 2; ++n)
                {
                    auto cred = STObject::makeInnerObject(sfCredential);
                    cred.setAccountID(sfIssuer, A2);
                    cred.setFieldVL(sfCredentialType, Slice("cred_type", 9));
                    credentials.push_back(std::move(cred));
                }
                slePd->setFieldArray(sfAcceptedCredentials, credentials);
                ac.view().insert(slePd);
                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase << "PermissionedDomain Set 1";
        doInvariantCheck(
            {{"permissioned domain with no rules."}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);

                // create PD
                createPermissionedDomain(ac, slePd, A1, A2);

                // update PD with empty rules
                {
                    STArray credentials(sfAcceptedCredentials, 2);
                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase << "PermissionedDomain Set 2";
        doInvariantCheck(
            {{"permissioned domain bad credentials size " +
              std::to_string(tooBig)}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);

                // create PD
                createPermissionedDomain(ac, slePd, A1, A2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, tooBig);

                    for (std::size_t n = 0; n < tooBig; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, A2);
                        auto credType = "cred_type2" + std::to_string(n);
                        cred.setFieldVL(
                            sfCredentialType,
                            Slice(credType.c_str(), credType.size()));
                        credentials.push_back(std::move(cred));
                    }

                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase << "PermissionedDomain Set 3";
        doInvariantCheck(
            {{"permissioned domain credentials aren't sorted"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);

                // create PD
                createPermissionedDomain(ac, slePd, A1, A2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, 2);
                    for (std::size_t n = 0; n < 2; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, A2);
                        auto credType =
                            std::string("cred_type2") + std::to_string(9 - n);
                        cred.setFieldVL(
                            sfCredentialType,
                            Slice(credType.c_str(), credType.size()));
                        credentials.push_back(std::move(cred));
                    }

                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase << "PermissionedDomain Set 4";
        doInvariantCheck(
            {{"permissioned domain credentials aren't unique"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);

                // create PD
                createPermissionedDomain(ac, slePd, A1, A2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, 2);
                    for (std::size_t n = 0; n < 2; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, A2);
                        cred.setFieldVL(
                            sfCredentialType, Slice("cred_type", 9));
                        credentials.push_back(std::move(cred));
                    }
                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
    }

    void
    testValidPseudoAccounts()
    {
        testcase << "valid pseudo accounts";

        using namespace jtx;

        AccountID pseudoAccountID;
        Preclose createPseudo =
            [&, this](Account const& a, Account const& b, Env& env) {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

                // Create vault
                Vault vault{env};
                auto [tx, vKeylet] =
                    vault.create({.owner = a, .asset = xrpAsset});
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
                "pseudo-account has 0 pseudo-account fields set",
                [this](SLE::pointer& sle) {
                    BEAST_EXPECT(sle->at(~sfVaultID));
                    sle->at(~sfVaultID) = std::nullopt;
                },
            },
            {
                "pseudo-account sequence changed",
                [](SLE::pointer& sle) { sle->at(sfSequence) = 12345; },
            },
            {
                "pseudo-account flags are not set",
                [](SLE::pointer& sle) { sle->at(sfFlags) = lsfNoFreeze; },
            },
            {
                "pseudo-account has a regular key",
                [](SLE::pointer& sle) {
                    sle->at(sfRegularKey) = Account("regular").id();
                },
            },
        });

        for (auto const& mod : mods)
        {
            doInvariantCheck(
                {{mod.expectedFailure}},
                [&](Account const& A1, Account const&, ApplyContext& ac) {
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
                [&](Account const& A1, Account const&, ApplyContext& ac) {
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
            [&](Account const& A1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                sle->at(sfSequence) = 0;
                ac.view().update(sle);
                return true;
            });
    }

    void
    testPermissionedDEX()
    {
        using namespace test::jtx;
        testcase << "PermissionedDEX";

        doInvariantCheck(
            {{"domain doesn't exist"}},
            [](Account const& A1, Account const&, ApplyContext& ac) {
                Keylet const offerKey = keylet::offer(A1.id(), 10);
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, A1);
                sleOffer->setFieldAmount(sfTakerPays, A1["USD"](10));
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
                        uint256{
                            "F10D0CC9A0F9A3CBF585B80BE09A186483668FDBDD39AA7E33"
                            "70F3649CE134E5"});
                    Account const A1{"A1"};
                    tx.setFieldAmount(sfTakerPays, A1["USD"](10));
                    tx.setFieldAmount(sfTakerGets, XRP(1));
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // missing domain ID in offer object
        doInvariantCheck(
            {{"hybrid offer is malformed"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                createPermissionedDomain(ac, slePd, A1, A2);

                Keylet const offerKey = keylet::offer(A2.id(), 10);
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, A2);
                sleOffer->setFieldAmount(sfTakerPays, A1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                sleOffer->setFlag(lsfHybrid);

                STArray bookArr;
                bookArr.push_back(STObject::makeInnerObject(sfBook));
                sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{ttOFFER_CREATE, [&](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // more than one entry in sfAdditionalBooks
        doInvariantCheck(
            {{"hybrid offer is malformed"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                createPermissionedDomain(ac, slePd, A1, A2);

                Keylet const offerKey = keylet::offer(A2.id(), 10);
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, A2);
                sleOffer->setFieldAmount(sfTakerPays, A1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                sleOffer->setFlag(lsfHybrid);
                sleOffer->setFieldH256(sfDomainID, pdKeylet.key);

                STArray bookArr;
                bookArr.push_back(STObject::makeInnerObject(sfBook));
                bookArr.push_back(STObject::makeInnerObject(sfBook));
                sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{ttOFFER_CREATE, [&](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // hybrid offer missing sfAdditionalBooks
        doInvariantCheck(
            {{"hybrid offer is malformed"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                createPermissionedDomain(ac, slePd, A1, A2);

                Keylet const offerKey = keylet::offer(A2.id(), 10);
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, A2);
                sleOffer->setFieldAmount(sfTakerPays, A1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                sleOffer->setFlag(lsfHybrid);
                sleOffer->setFieldH256(sfDomainID, pdKeylet.key);
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{ttOFFER_CREATE, [&](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {{"transaction consumed wrong domains"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                createPermissionedDomain(ac, slePd, A1, A2);

                Keylet const badDomainKeylet =
                    keylet::permissionedDomain(A1.id(), 20);
                auto sleBadPd = std::make_shared<SLE>(badDomainKeylet);
                createPermissionedDomain(ac, sleBadPd, A1, A2);

                Keylet const offerKey = keylet::offer(A2.id(), 10);
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, A2);
                sleOffer->setFieldAmount(sfTakerPays, A1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                sleOffer->setFieldH256(sfDomainID, pdKeylet.key);
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{
                ttOFFER_CREATE,
                [&](STObject& tx) {
                    Account const A1{"A1"};
                    Keylet const badDomainKey =
                        keylet::permissionedDomain(A1.id(), 20);
                    tx.setFieldH256(sfDomainID, badDomainKey.key);
                    tx.setFieldAmount(sfTakerPays, A1["USD"](10));
                    tx.setFieldAmount(sfTakerGets, XRP(1));
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {{"domain transaction affected regular offers"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                Keylet const pdKeylet = keylet::permissionedDomain(A1.id(), 10);
                auto slePd = std::make_shared<SLE>(pdKeylet);
                createPermissionedDomain(ac, slePd, A1, A2);

                Keylet const offerKey = keylet::offer(A2.id(), 10);
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, A2);
                sleOffer->setFieldAmount(sfTakerPays, A1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{
                ttOFFER_CREATE,
                [&](STObject& tx) {
                    Account const A1{"A1"};
                    Keylet const domainKey =
                        keylet::permissionedDomain(A1.id(), 10);
                    tx.setFieldH256(sfDomainID, domainKey.key);
                    tx.setFieldAmount(sfTakerPays, A1["USD"](10));
                    tx.setFieldAmount(sfTakerGets, XRP(1));
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
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
            std::optional<int> assetsTotal = {};
            std::optional<int> assetsAvailable = {};
            std::optional<int> lossUnrealized = {};
            std::optional<int> assetsMaximum = {};
            std::optional<int> sharesTotal = {};
            std::optional<int> vaultAssets = {};
            std::optional<AccountAmount> accountAssets = {};
            std::optional<AccountAmount> accountShares = {};
        };
        auto constexpr adjust = [&](ApplyView& ac,
                                    ripple::Keylet keylet,
                                    Adjustments args) {
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
                (*sleVault)[sfAssetsTotal] =
                    *(*sleVault)[sfAssetsTotal] + *args.assetsTotal;
            if (args.assetsAvailable)
                (*sleVault)[sfAssetsAvailable] =
                    *(*sleVault)[sfAssetsAvailable] + *args.assetsAvailable;
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
                    (*sleMPToken)[sfMPTAmount] =
                        *(*sleMPToken)[sfMPTAmount] + *args.vaultAssets;
                    ac.update(sleMPToken);
                }
                else
                    return false;  // Not supporting testing with IOU
            }

            if (args.accountAssets)
            {
                auto const& pair = *args.accountAssets;
                if (assets.native())
                {
                    auto sleAccount = ac.peek(keylet::account(pair.account));
                    if (!sleAccount)
                        return false;
                    (*sleAccount)[sfBalance] =
                        *(*sleAccount)[sfBalance] + pair.amount;
                    ac.update(sleAccount);
                }
                else if (assets.holds<MPTIssue>())
                {
                    auto const mptID = assets.get<MPTIssue>().getMptID();
                    auto sleMPToken =
                        ac.peek(keylet::mptoken(mptID, pair.account));
                    if (!sleMPToken)
                        return false;
                    (*sleMPToken)[sfMPTAmount] =
                        *(*sleMPToken)[sfMPTAmount] + pair.amount;
                    ac.update(sleMPToken);
                }
                else
                    return false;  // Not supporting testing with IOU
            }

            if (args.accountShares)
            {
                auto const& pair = *args.accountShares;
                auto sleMPToken =
                    ac.peek(keylet::mptoken(mptIssuanceID, pair.account));
                if (!sleMPToken)
                    return false;
                (*sleMPToken)[sfMPTAmount] =
                    *(*sleMPToken)[sfMPTAmount] + pair.amount;
                ac.update(sleMPToken);
            }
            return true;
        };

        constexpr auto args =
            [](AccountID id, int adjustment, auto fn) -> Adjustments {
            Adjustments sample = {
                .assetsTotal = adjustment,
                .assetsAvailable = adjustment,
                .lossUnrealized = 0,
                .sharesTotal = adjustment,
                .vaultAssets = adjustment,
                .accountAssets =  //
                AccountAmount{id, -adjustment},
                .accountShares =  //
                AccountAmount{id, adjustment}};
            fn(sample);
            return sample;
        };

        Account A3{"A3"};
        Account A4{"A4"};
        auto const precloseXrp =
            [&](Account const& A1, Account const& A2, Env& env) -> bool {
            env.fund(XRP(1000), A3, A4);
            Vault vault{env};
            auto [tx, keylet] =
                vault.create({.owner = A1, .asset = xrpIssue()});
            env(tx);
            env(vault.deposit(
                {.depositor = A1, .id = keylet.key, .amount = XRP(10)}));
            env(vault.deposit(
                {.depositor = A2, .id = keylet.key, .amount = XRP(10)}));
            env(vault.deposit(
                {.depositor = A3, .id = keylet.key, .amount = XRP(10)}));
            return true;
        };

        testcase << "Vault general checks";
        doInvariantCheck(
            {"vault deletion succeeded without deleting a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(A1.id(), sequence);
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(A1.id()),
                    sleVault->key(),
                    describeOwnerDir(A1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);
                ac.view().insert(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {"vault deleted by a wrong transaction type"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation updated more than single vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                {
                    auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                    auto sleVault = ac.view().peek(keylet);
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);
                }
                {
                    auto const keylet = keylet::vault(A2.id(), ac.view().seq());
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
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                {
                    auto [tx, _] =
                        vault.create({.owner = A1, .asset = xrpIssue()});
                    env(tx);
                }
                {
                    auto [tx, _] =
                        vault.create({.owner = A2, .asset = xrpIssue()});
                    env(tx);
                }
                return true;
            });

        doInvariantCheck(
            {"vault operation updated more than single vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const insertVault = [&](Account const A) {
                    auto const vaultKeylet = keylet::vault(A.id(), sequence);
                    auto sleVault = std::make_shared<SLE>(vaultKeylet);
                    auto const vaultPage = ac.view().dirInsert(
                        keylet::ownerDir(A.id()),
                        sleVault->key(),
                        describeOwnerDir(A.id()));
                    sleVault->setFieldU64(sfOwnerNode, *vaultPage);
                    ac.view().insert(sleVault);
                };
                insertVault(A1);
                insertVault(A2);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {"deleted vault must also delete shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"deleted vault must have no shares outstanding",
             "deleted vault must have no assets outstanding",
             "deleted vault must have no assets available"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(
                    keylet::mptIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().erase(sleVault);
                ac.view().erase(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                env(vault.deposit(
                    {.depositor = A1, .id = keylet.key, .amount = XRP(10)}));
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(
                    keylet::mptIssuance((*sleVault)[sfShareMPTID]));
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"updated vault must have shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsMaximum] = 200;
                ac.view().update(sleVault);

                auto sleShares = ac.view().peek(
                    keylet::mptIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().erase(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without updating shares",
             "assets available must not be greater than assets outstanding"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
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
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                env(vault.deposit(
                    {.depositor = A1, .id = keylet.key, .amount = XRP(10)}));
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto slePseudoAccount =
                    ac.view().peek(keylet::account(*(*sleVault)[sfAccount]));
                if (!slePseudoAccount)
                    return false;
                (*slePseudoAccount)[sfBalance] =
                    *(*slePseudoAccount)[sfBalance] - 10;
                ac.view().update(slePseudoAccount);

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(A4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 0, [&](Adjustments& sample) {
                        sample.assetsAvailable = (DROPS_PER_XRP * -100).value();
                        sample.assetsTotal = (DROPS_PER_XRP * -200).value();
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldIssue(
                    sfAsset, STIssue{sfAsset, MPTIssue(MPTID(42))});
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setAccountID(sfAccount, A2.id());
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 0, [&](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 100, [&](Adjustments& sample) {
                        sample.lossUnrealized = 13;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [](STObject& tx) {
                    tx.setFieldAmount(sfAmount, XRPAmount(200));
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"set assets outstanding must not exceed assets maximum"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 0, [&](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 0, [&](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                auto sleShares = ac.view().peek(
                    keylet::mptIssuance((*sleVault)[sfShareMPTID]));
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(
                    keylet::mptIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfMaximumAmount] = 10;
                ac.view().update(sleShares);

                return adjust(
                    ac.view(), keylet, args(A2.id(), 10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                adjust(
                    ac.view(), keylet, args(A2.id(), 10, [](Adjustments&) {}));

                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(
                    keylet::mptIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfOutstandingAmount] = maxMPTokenAmount + 1;
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
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
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = A1, .asset = xrpIssue()});
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
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
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = A1, .asset = xrpIssue()});
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
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
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "created vault must be empty",
                "create operation must not have updated a vault",
            },
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(
                    keylet::mptIssuance((*sleVault)[sfShareMPTID]));
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
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "assets maximum must be positive",
                "create operation must not have updated a vault",
            },
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
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
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"create operation must not have updated a vault",
             "shares issuer and vault pseudo-account must be the same",
             "shares issuer must be a pseudo-account",
             "shares issuer pseudo-account must point back to the vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(
                    keylet::mptIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().update(sleVault);
                (*sleShares)[sfIssuer] = A1.id();
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault created by a wrong transaction type",
             "account root created illegally"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                // The code below will create a valid vault with (almost) all
                // the invariants holding. Except one: it is created by the
                // wrong transaction type.
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(A1.id(), sequence);
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(A1.id()),
                    sleVault->key(),
                    describeOwnerDir(A1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto pseudoId =
                    pseudoAccountAddress(ac.view(), vaultKeylet.key);
                // Create pseudo-account.
                auto sleAccount =
                    std::make_shared<SLE>(keylet::account(pseudoId));
                sleAccount->setAccountID(sfAccount, pseudoId);
                sleAccount->setFieldAmount(sfBalance, STAmount{});
                std::uint32_t const seqno =                             //
                    ac.view().rules().enabled(featureSingleAssetVault)  //
                    ? 0                                                 //
                    : sequence;
                sleAccount->setFieldU32(sfSequence, seqno);
                sleAccount->setFieldU32(
                    sfFlags,
                    lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
                ac.view().insert(sleAccount);

                auto const sharesMptId = makeMptID(sequence, pseudoId);
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(pseudoId),
                    sharesKeylet,
                    describeOwnerDir(pseudoId));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                sleShares->at(sfIssuer) = pseudoId;
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                sleVault->at(sfAccount) = pseudoId;
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = A1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) =
                    vaultStrategyFirstComeFirstServe;

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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(A1.id(), sequence);
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(A1.id()),
                    sleVault->key(),
                    describeOwnerDir(A1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto pseudoId =
                    pseudoAccountAddress(ac.view(), vaultKeylet.key);
                // Create pseudo-account.
                auto sleAccount =
                    std::make_shared<SLE>(keylet::account(pseudoId));
                sleAccount->setAccountID(sfAccount, pseudoId);
                sleAccount->setFieldAmount(sfBalance, STAmount{});
                std::uint32_t const seqno =                             //
                    ac.view().rules().enabled(featureSingleAssetVault)  //
                    ? 0                                                 //
                    : sequence;
                sleAccount->setFieldU32(sfSequence, seqno);
                sleAccount->setFieldU32(
                    sfFlags,
                    lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                // sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
                // Setting wrong vault key
                sleAccount->setFieldH256(sfVaultID, uint256(42));
                ac.view().insert(sleAccount);

                auto const sharesMptId = makeMptID(sequence, pseudoId);
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(pseudoId),
                    sharesKeylet,
                    describeOwnerDir(pseudoId));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                sleShares->at(sfIssuer) = pseudoId;
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                // sleVault->at(sfAccount) = pseudoId;
                // Setting wrong pseudo acocunt ID
                sleVault->at(sfAccount) = A2.id();
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = A1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) =
                    vaultStrategyFirstComeFirstServe;

                ac.view().insert(sleVault);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        doInvariantCheck(
            {"shares issuer and vault pseudo-account must be the same",
             "shares issuer must exist"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(A1.id(), sequence);
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(A1.id()),
                    sleVault->key(),
                    describeOwnerDir(A1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto const sharesMptId = makeMptID(sequence, A2.id());
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(A2.id()),
                    sharesKeylet,
                    describeOwnerDir(A2.id()));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                // Setting wrong pseudo acocunt ID
                sleShares->at(sfIssuer) = AccountID(uint160(42));
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                sleVault->at(sfAccount) = A2.id();
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = A1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) =
                    vaultStrategyFirstComeFirstServe;

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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 0, [](Adjustments& sample) {
                        sample.vaultAssets.reset();
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"deposit assets outstanding must not exceed assets maximum"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 200, [&](Adjustments& sample) {
                        sample.assetsMaximum = 1;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [](STObject& tx) {
                    tx.setFieldAmount(sfAmount, XRPAmount(200));
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // This really convoluted unit tests makes the zero balance on the
        // depositor, by sending them the same amount as the transaction fee.
        // The operation makes no sense, but the defensive check in
        // ValidVault::finalize is otherwise impossible to trigger.
        doInvariantCheck(
            {"deposit must increase vault balance",
             "deposit must change depositor balance"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(A4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return adjust(
                    ac.view(),
                    keylet,
                    args(A3.id(), -10, [&](Adjustments& sample) {
                        sample.accountAssets->amount = -100;
                    }));
            },
            XRPAmount{100},
            STTx{
                ttVAULT_DEPOSIT,
                [&](STObject& tx) {
                    tx[sfFee] = XRPAmount(100);
                    tx[sfAccount] = A3.id();
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"deposit must increase vault balance",
             "deposit must decrease depositor balance",
             "deposit must change vault and depositor balance by equal amount",
             "deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                // Move 10 drops from A2 to A3 to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(A3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 10;
                ac.view().update(sleA3);

                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 10, [&](Adjustments& sample) {
                        sample.vaultAssets = -20;
                        sample.accountAssets->amount = 10;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change depositor balance"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                // Move 10 drops from A3 to vault to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(A3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 10;
                ac.view().update(sleA3);

                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 10, [&](Adjustments& sample) {
                        sample.accountAssets->amount = 0;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change depositor shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 10, [&](Adjustments& sample) {
                        sample.accountShares.reset();
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change vault shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 10, [](Adjustments& sample) {
                        sample.sharesTotal = 0;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must increase depositor shares",
             "deposit must change depositor and vault shares by equal amount",
             "deposit must not change vault balance by more than deposited "
             "amount"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 10, [&](Adjustments& sample) {
                        sample.accountShares->amount = -5;
                        sample.sharesTotal = -10;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [](STObject& tx) { tx[sfAmount] = XRPAmount(5); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit and assets outstanding must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto sleA3 = ac.view().peek(keylet::account(A3.id()));
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 2000;
                ac.view().update(sleA3);

                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 10, [&](Adjustments& sample) {
                        sample.assetsTotal = 11;
                    }));
            },
            XRPAmount{2000},
            STTx{
                ttVAULT_DEPOSIT,
                [&](STObject& tx) {
                    tx[sfAmount] = XRPAmount(10);
                    tx[sfDelegate] = A3.id();
                    tx[sfFee] = XRPAmount(2000);
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 10, [&](Adjustments& sample) {
                        sample.assetsTotal = 7;
                        sample.assetsAvailable = 7;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        testcase << "Vault withdrawal";
        doInvariantCheck(
            {"withdrawal must change vault balance"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 0, [](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(A4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return adjust(
                    ac.view(),
                    keylet,
                    args(A3.id(), -10, [&](Adjustments& sample) {
                        sample.accountAssets->amount = -100;
                    }));
            },
            XRPAmount{100},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) {
                    tx[sfFee] = XRPAmount(100);
                    tx[sfAccount] = A3.id();
                    // This commented out line causes the invariant violation.
                    // tx[sfDestination] = A4.id();
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"withdrawal must change vault and destination balance by "
             "equal amount",
             "withdrawal must decrease vault balance",
             "withdrawal must increase destination balance",
             "withdrawal and assets outstanding must add up",
             "withdrawal and assets available must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                // Move 10 drops from A2 to A3 to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(A3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 10;
                ac.view().update(sleA3);

                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), -10, [&](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                if (!adjust(
                        ac.view(),
                        keylet,
                        args(A2.id(), -10, [&](Adjustments& sample) {
                            *sample.vaultAssets -= 5;
                        })))
                    return false;
                auto sleA3 = ac.view().peek(keylet::account(A3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 5;
                ac.view().update(sleA3);
                return true;
            },
            XRPAmount{},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) { tx.setAccountID(sfDestination, A3.id()); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change depositor shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), -10, [&](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), -10, [](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), -10, [&](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), -10, [&](Adjustments& sample) {
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto sleA3 = ac.view().peek(keylet::account(A3.id()));
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 2000;
                ac.view().update(sleA3);

                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), -10, [&](Adjustments& sample) {
                        sample.assetsTotal = -7;
                    }));
            },
            XRPAmount{2000},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) {
                    tx[sfAmount] = XRPAmount(10);
                    tx[sfDelegate] = A3.id();
                    tx[sfFee] = XRPAmount(2000);
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        auto const precloseMpt =
            [&](Account const& A1, Account const& A2, Env& env) -> bool {
            env.fund(XRP(1000), A3, A4);

            // Create MPT asset
            {
                Json::Value jv;
                jv[sfAccount] = A3.human();
                jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
                jv[sfFlags] = tfMPTCanTransfer;
                env(jv);
                env.close();
            }

            auto const mptID = makeMptID(env.seq(A3) - 1, A3);
            Asset asset = MPTIssue(mptID);
            // Authorize A1 A2 A4
            {
                Json::Value jv;
                jv[sfAccount] = A1.human();
                jv[sfTransactionType] = jss::MPTokenAuthorize;
                jv[sfMPTokenIssuanceID] = to_string(mptID);
                env(jv);
                jv[sfAccount] = A2.human();
                env(jv);
                jv[sfAccount] = A4.human();
                env(jv);

                env.close();
            }
            // Send tokens to A1 A2 A4
            {
                env(pay(A3, A1, asset(1000)));
                env(pay(A3, A2, asset(1000)));
                env(pay(A3, A4, asset(1000)));
                env.close();
            }

            Vault vault{env};
            auto [tx, keylet] = vault.create({.owner = A1, .asset = asset});
            env(tx);
            env(vault.deposit(
                {.depositor = A1, .id = keylet.key, .amount = asset(10)}));
            env(vault.deposit(
                {.depositor = A2, .id = keylet.key, .amount = asset(10)}));
            env(vault.deposit(
                {.depositor = A4, .id = keylet.key, .amount = asset(10)}));
            return true;
        };

        doInvariantCheck(
            {"withdrawal must decrease depositor shares",
             "withdrawal must change depositor and vault shares by equal "
             "amount"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq() - 2);
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), -10, [&](Adjustments& sample) {
                        sample.accountShares->amount = 5;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) { tx[sfAccount] = A3.id(); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt,
            TxAccount::A2);

        testcase << "Vault clawback";
        doInvariantCheck(
            {"clawback must change vault balance"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq() - 2);
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), -1, [&](Adjustments& sample) {
                        sample.vaultAssets.reset();
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) { tx[sfAccount] = A3.id(); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        // Not the same as below check: attempt to clawback XRP
        doInvariantCheck(
            {"clawback may only be performed by the asset issuer"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 0, [&](Adjustments& sample) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // Not the same as above check: attempt to clawback MPT by bad account
        doInvariantCheck(
            {"clawback may only be performed by the asset issuer"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq() - 2);
                return adjust(
                    ac.view(),
                    keylet,
                    args(A2.id(), 0, [&](Adjustments& sample) {}));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) { tx[sfAccount] = A4.id(); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must decrease vault balance",
             "clawback must decrease holder shares",
             "clawback must change vault shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq() - 2);
                return adjust(
                    ac.view(),
                    keylet,
                    args(A4.id(), 10, [&](Adjustments& sample) {
                        sample.sharesTotal = 0;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = A3.id();
                    tx[sfHolder] = A4.id();
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must change holder shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq() - 2);
                return adjust(
                    ac.view(),
                    keylet,
                    args(A4.id(), -10, [&](Adjustments& sample) {
                        sample.accountShares.reset();
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = A3.id();
                    tx[sfHolder] = A4.id();
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must change holder and vault shares by equal amount",
             "clawback and assets outstanding must add up",
             "clawback and assets available must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq() - 2);
                return adjust(
                    ac.view(),
                    keylet,
                    args(A4.id(), -10, [&](Adjustments& sample) {
                        sample.accountShares->amount = -8;
                        sample.assetsTotal = -7;
                        sample.assetsAvailable = -7;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = A3.id();
                    tx[sfHolder] = A4.id();
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);
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
        testPermissionedDomainInvariants();
        testValidPseudoAccounts();
        testPermissionedDEX();
        testVault();
    }
};

BEAST_DEFINE_TESTSUITE(Invariants, app, ripple);

}  // namespace test
}  // namespace ripple
