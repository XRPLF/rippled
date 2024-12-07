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
#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <boost/algorithm/string/predicate.hpp>

namespace ripple {

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
        FeatureBitset amendments =
            supported_amendments() | featureInvariantsV1_1;
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
            BEAST_EXPECT(
                sink.messages().str().starts_with("Invariant failed:") ||
                sink.messages().str().starts_with(
                    "Transaction caused an exception"));
            // uncomment if you want to log the invariant failure message
            // log << "   --> " << sink.messages().str() << std::endl;
            for (auto const& m : expect_logs)
            {
                BEAST_EXPECT(
                    sink.messages().str().find(m) != std::string::npos);
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

        // doInvariantCheck(
        //     {{"Cannot return non-native STAmount as XRPAmount"}},
        //     [](Account const& A1, Account const& A2, ApplyContext& ac) {
        //         // escrow with nonnative amount
        //         auto const sle = ac.view().peek(keylet::account(A1.id()));
        //         if (!sle)
        //             return false;
        //         auto sleNew = std::make_shared<SLE>(
        //             keylet::escrow(A1, (*sle)[sfSequence] + 2));
        //         STAmount nonNative(A2["USD"](51));
        //         sleNew->setFieldAmount(sfAmount, nonNative);
        //         ac.view().insert(sleNew);
        //         return true;
        //     });

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
    }

    void
    testValidNewAccountRoot()
    {
        using namespace test::jtx;
        testcase << "valid new account root";

        doInvariantCheck(
            {{"account root created by a non-Payment"}},
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

public:
    void
    run() override
    {
        testXRPNotCreated();
        testAccountRootsNotRemoved();
        testAccountRootsDeletedClean();
        testTypesMatch();
        testNoXRPTrustLine();
        testXRPBalanceCheck();
        testTransactionFeeCheck();
        testNoBadOffers();
        testNoZeroEscrow();
        testValidNewAccountRoot();
        testNFTokenPageInvariants();
    }
};

BEAST_DEFINE_TESTSUITE(Invariants, ledger, ripple);

}  // namespace ripple
