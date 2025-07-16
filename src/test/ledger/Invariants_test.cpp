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

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/STLedgerEntry.h>

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
     *
     */
    void
    doInvariantCheck(
        std::vector<std::string> const& expect_logs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        std::initializer_list<TER> ters =
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        Preclose const& preclose = {})
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
            for (auto const& m : expect_logs)
            {
                if (messages.find(m) == std::string::npos)
                {
                    // uncomment if you want to log the invariant failure
                    // message log << "   --> " << m << std::endl;
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
                auto sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                sle->at(sfBalance) = beast::zero;
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
                auto sleA1 = ac.view().peek(keylet::account(A1.id()));
                auto sleA2 = ac.view().peek(keylet::account(A2.id()));
                if (!sleA1 || !sleA2)
                    return false;
                sleA1->at(sfBalance) = beast::zero;
                sleA2->at(sfBalance) = beast::zero;
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
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                // A1 has a balance. Delete A1
                auto const a1 = A1.id();
                auto const sleA1 = ac.view().peek(keylet::account(a1));
                if (!sleA1)
                    return false;
                if (!BEAST_EXPECT(*sleA1->at(sfBalance) != beast::zero))
                    return false;

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

        doInvariantCheck(
            {{"account deletion left behind a non-zero owner count"}},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                // Increment A1's owner count, then delete A1
                auto const a1 = A1.id();
                auto const sleA1 = ac.view().peek(keylet::account(a1));
                if (!sleA1)
                    return false;
                sleA1->at(sfBalance) = beast::zero;
                BEAST_EXPECT(sleA1->at(sfOwnerCount) == 0);
                adjustOwnerCount(ac.view(), sleA1, 1, ac.journal);

                ac.view().erase(sleA1);

                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});

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
                    auto sleA1 = ac.view().peek(keylet::account(a1));
                    if (!sleA1)
                        return false;

                    auto const key = std::invoke(keyletfunc, a1);
                    auto const newSLE = std::make_shared<SLE>(key);
                    ac.view().insert(newSLE);
                    sleA1->at(sfBalance) = beast::zero;
                    ac.view().erase(sleA1);

                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_DELETE, [](STObject& tx) {}});
        }

        // NFT special case
        doInvariantCheck(
            {{"account deletion left behind a NFTokenPage object"}},
            [&](Account const& A1, Account const&, ApplyContext& ac) {
                // remove an account from the view
                auto sle = ac.view().peek(keylet::account(A1.id()));
                if (!sle)
                    return false;
                sle->at(sfBalance) = beast::zero;
                sle->at(sfOwnerCount) = 0;
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
                auto sle = ac.view().peek(keylet::account(ammAcctID));
                if (!sle)
                    return false;

                BEAST_EXPECT(sle->at(~sfAMMID));
                BEAST_EXPECT(sle->at(~sfAMMID) == ammKey);

                sle->at(sfBalance) = beast::zero;
                sle->at(sfOwnerCount) = 0;
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
                auto sle = ac.view().peek(keylet::account(ammAcctID));
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

                sle->at(sfBalance) = beast::zero;
                sle->at(sfOwnerCount) = 0;
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

    Keylet
    createLoanBroker(
        jtx::Account const& a,
        jtx::Env& env,
        jtx::PrettyAsset const& asset)
    {
        using namespace jtx;

        // Create vault
        uint256 vaultID;
        Vault vault{env};
        auto [tx, vKeylet] = vault.create({.owner = a, .asset = asset});
        env(tx);
        BEAST_EXPECT(env.le(vKeylet));

        vaultID = vKeylet.key;

        // Create Loan Broker
        using namespace loanBroker;

        auto const loanBrokerKeylet = keylet::loanbroker(a.id(), env.seq(a));
        // Create a Loan Broker with all default values.
        env(set(a, vaultID), fee(increment));

        return loanBrokerKeylet;
    };

    void
    testNoModifiedUnmodifiableFields()
    {
        testcase("no modified unmodifiable fields");
        using namespace jtx;

        // Initialize with a placeholder value because there's no default ctor
        Keylet loanBrokerKeylet = keylet::amendments();
        Preclose createLoanBroker =
            [&, this](Account const& a, Account const& b, Env& env) {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

                loanBrokerKeylet = this->createLoanBroker(a, env, xrpAsset);
                return BEAST_EXPECT(env.le(loanBrokerKeylet));
            };

        {
            auto const mods =
                std::to_array<std::function<void(SLE::pointer&)>>({
                    [](SLE::pointer& sle) { sle->at(sfSequence) += 1; },
                    [](SLE::pointer& sle) { sle->at(sfOwnerNode) += 1; },
                    [](SLE::pointer& sle) { sle->at(sfVaultNode) += 1; },
                    [](SLE::pointer& sle) { sle->at(sfVaultID) = uint256(1u); },
                    [](SLE::pointer& sle) {
                        sle->at(sfAccount) = sle->at(sfOwner);
                    },
                    [](SLE::pointer& sle) {
                        sle->at(sfOwner) = sle->at(sfAccount);
                    },
                    [](SLE::pointer& sle) {
                        sle->at(sfManagementFeeRate) += 1;
                    },
                    [](SLE::pointer& sle) { sle->at(sfCoverRateMinimum) += 1; },
                    [](SLE::pointer& sle) {
                        sle->at(sfCoverRateLiquidation) += 1;
                    },
                    [](SLE::pointer& sle) { sle->at(sfLedgerEntryType) += 1; },
                    [](SLE::pointer& sle) {
                        sle->at(sfLedgerIndex) = sle->at(sfVaultID).value();
                    },
                });

            for (auto const& mod : mods)
            {
                doInvariantCheck(
                    {{"changed an unchangable field"}},
                    [&](Account const& A1, Account const&, ApplyContext& ac) {
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
            auto const mods =
                std::to_array<std::function<void(SLE::pointer&)>>({
                    [](SLE::pointer& sle) { sle->at(sfLedgerEntryType) += 1; },
                    [](SLE::pointer& sle) {
                        sle->at(sfLedgerIndex) = uint256(1u);
                    },
                });

            for (auto const& mod : mods)
            {
                doInvariantCheck(
                    {{"changed an unchangable field"}},
                    [&](Account const& A1, Account const&, ApplyContext& ac) {
                        auto sle = ac.view().peek(keylet::account(A1.id()));
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
                "pseudo-account has 2 pseudo-account fields set",
                [this](SLE::pointer& sle) {
                    BEAST_EXPECT(sle->at(~sfVaultID) && !sle->at(~sfAMMID));
                    sle->at(~sfAMMID) = ~sle->at(~sfVaultID);
                },
            },
            {
                "pseudo-account has 2 pseudo-account fields set",
                [this](SLE::pointer& sle) {
                    BEAST_EXPECT(
                        sle->at(~sfVaultID) && !sle->at(~sfLoanBrokerID));
                    sle->at(~sfLoanBrokerID) = ~sle->at(~sfVaultID);
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
    testValidLoanBroker()
    {
        testcase << "valid loan broker";

        using namespace jtx;

        enum class Asset { XRP, IOU, MPT };
        auto const assetTypes =
            std::to_array({Asset::XRP, Asset::IOU, Asset::MPT});

        for (auto const assetType : assetTypes)
        {
            // Initialize with a placeholder value because there's no default
            // ctor
            Keylet loanBrokerKeylet = keylet::amendments();
            Preclose createLoanBroker = [&, this](
                                            Account const& alice,
                                            Account const& issuer,
                                            Env& env) {
                PrettyAsset const asset = [&]() {
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
                            MPTTester mptt{env, issuer, mptInitNoFund};
                            mptt.create(
                                {.flags = tfMPTCanClawback | tfMPTCanTransfer |
                                     tfMPTCanLock});
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
                }();
                loanBrokerKeylet = this->createLoanBroker(alice, env, asset);
                return BEAST_EXPECT(env.le(loanBrokerKeylet));
            };

            // Ensure the test scenarios are set up completely. The test cases
            // will need to recompute any of these values it needs for itself
            // rather than trying to return a bunch of items
            auto setupTest =
                [&, this](Account const& A1, Account const&, ApplyContext& ac)
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
                auto const pseudoAccountKeylet =
                    keylet::account(pseudoAccountID);
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
                        directory::createRoot(
                            ac.view(),
                            dirKeylet,
                            loanBrokerKeylet.key,
                            describe) == 0);

                    sleDir = ac.view().peek(dirKeylet);
                }

                return std::make_pair(slePseudo, sleDir);
            };

            doInvariantCheck(
                {{"Loan Broker with zero OwnerCount has multiple directory "
                  "pages"}},
                [&setupTest, this](
                    Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto test = setupTest(A1, A2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto const describe =
                        describeOwnerDir(slePseudo->at(sfAccount));

                    BEAST_EXPECT(
                        directory::insertPage(
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
                [&setupTest](
                    Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto test = setupTest(A1, A2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto indexes = sleDir->getFieldV256(sfIndexes);

                    // Put some extra garbage into the directory
                    for (auto const& key : {slePseudo->key(), sleDir->key()})
                    {
                        directory::insertKey(
                            ac.view(), sleDir, 0, false, indexes, key);
                    }

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker directory corrupt"}},
                [&setupTest](
                    Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto test = setupTest(A1, A2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto const describe =
                        describeOwnerDir(slePseudo->at(sfAccount));
                    // Empty vector will overwrite the existing entry for the
                    // holding, if any, avoiding the "has multiple indexes"
                    // failure.
                    STVector256 indexes;

                    // Put one meaningless key into the directory
                    auto const key =
                        keylet::account(Account("random").id()).key;
                    directory::insertKey(
                        ac.view(), sleDir, 0, false, indexes, key);

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker with zero OwnerCount has an unexpected entry in "
                  "the directory"}},
                [&setupTest](
                    Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto test = setupTest(A1, A2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    // Empty vector will overwrite the existing entry for the
                    // holding, if any, avoiding the "has multiple indexes"
                    // failure.
                    STVector256 indexes;

                    directory::insertKey(
                        ac.view(), sleDir, 0, false, indexes, slePseudo->key());

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker sequence number decreased"}},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
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
        testPermissionedDomainInvariants();
        testPermissionedDEX();
        testNoModifiedUnmodifiableFields();
        testValidPseudoAccounts();
        testValidLoanBroker();
    }
};

BEAST_DEFINE_TESTSUITE(Invariants, ledger, ripple);

}  // namespace test
}  // namespace ripple
