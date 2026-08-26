#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/token.h>
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
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/invariants/InvariantRunner.h>

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsMisc_test : public InvariantsBase
{
    FeatureBitset const all_{test::jtx::testableAmendments()};

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
                Env env{*this, all_};
                Account const a1{"A1"};
                env.fund(XRP(1000), a1);
                env.close();

                OpenView ov{*env.current()};

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a1.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
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
                Env env{*this, all_ - featureLendingProtocolV1_1};
                Account const a1{"A1"};
                env.fund(XRP(1000), a1);
                env.close();

                OpenView ov{*env.current()};

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a1.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
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

        // ValidLoan::finalize enforces (under featureLendingProtocolV1_1) that
        // interest due - TotalValueOutstanding minus PrincipalOutstanding minus
        // ManagementFeeOutstanding - is never negative. Any rounding path in
        // LoanPay / LoanManage that rounds Principal or ManagementFee up while
        // rounding TotalValue down (or vice-versa) by a single ULP flips this
        // negative and would halt the ledger. Exercise each of the three
        // components at a one-ULP overshoot to cover the boundary explicitly,
        // plus the exact-zero case to confirm the boundary itself is
        // accepted.
        //
        // The three components are each rounded to sfLoanScale independently,
        // so for a non-integral (IOU) asset one unit at that scale is absorbed
        // as quantization noise; for an integral asset (XRP/MPT) the boundary
        // is enforced strictly. Every case below is therefore run twice: once
        // against a real XRP-backed broker, so finalize resolves the vault
        // asset and takes the strict path, and once against a synthetic broker
        // ID, so the vault cannot be resolved and the tolerant path is taken.
        // makeLoanSle leaves sfLoanScale at its SoeDefault of 0, making the
        // tolerance exactly one unit - the same size as the perturbation, which
        // is what separates the two paths.
        {
            struct Case
            {
                Number totalValue;
                Number principal;
                Number managementFee;
                bool expectFireIntegral;
                bool expectFireTolerant;
            };
            // Baseline: Principal=100, TotalValue=100, MgmtFee=0
            // (interest due = 0, exactly at the boundary). The middle cases
            // perturb one component by -1 or +1 so interest due = -1, which is
            // within the tolerance; the last overshoots it at -2.
            auto const cases = std::to_array<Case>({
                {.totalValue = Number(100),
                 .principal = Number(100),
                 .managementFee = Number(0),
                 .expectFireIntegral = false,
                 .expectFireTolerant = false},
                {.totalValue = Number(99),
                 .principal = Number(100),
                 .managementFee = Number(0),
                 .expectFireIntegral = true,
                 .expectFireTolerant = false},
                {.totalValue = Number(100),
                 .principal = Number(101),
                 .managementFee = Number(0),
                 .expectFireIntegral = true,
                 .expectFireTolerant = false},
                {.totalValue = Number(100),
                 .principal = Number(100),
                 .managementFee = Number(1),
                 .expectFireIntegral = true,
                 .expectFireTolerant = false},
                {.totalValue = Number(98),
                 .principal = Number(100),
                 .managementFee = Number(0),
                 .expectFireIntegral = true,
                 .expectFireTolerant = true},
            });

            for (bool const integralAsset : {true, false})
            {
                for (auto const& c : cases)
                {
                    Env env{*this, all_};
                    Account const a1{"A1"};
                    env.fund(XRP(1000), a1);
                    env.close();

                    std::optional<Keylet> realBrokerKeylet;
                    if (integralAsset)
                    {
                        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
                        realBrokerKeylet = this->createLoanBroker(a1, env, xrpAsset);
                        if (!BEAST_EXPECT(env.le(*realBrokerKeylet)))
                            continue;
                        env.close();
                    }

                    OpenView ov{*env.current()};

                    auto const brokerKeylet = realBrokerKeylet.value_or(
                        keylet::loanBroker(a1.id(), SeqProxy::rawSequence(ov.seq())));
                    auto const loanKeylet =
                        keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                    // Seed a loan whose interest due sits at the boundary
                    // (100 - 100 - 0 = 0). The apply-view update below moves it.
                    {
                        auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a1.id());
                        sleLoan->at(sfPrincipalOutstanding) = Number(100);
                        sleLoan->at(sfTotalValueOutstanding) = Number(100);
                        sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                        sleLoan->setFieldU32(sfPaymentRemaining, 1);
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
                    sleLoan->at(sfTotalValueOutstanding) = c.totalValue;
                    sleLoan->at(sfPrincipalOutstanding) = c.principal;
                    sleLoan->at(sfManagementFeeOutstanding) = c.managementFee;
                    ac.view().update(sleLoan);

                    auto transactor = makeTransactor(ac);
                    if (!BEAST_EXPECT(transactor))
                        continue;
                    TER const result = transactor->checkInvariants(
                        tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                    auto const messages = sink.messages().str();
                    if (integralAsset ? c.expectFireIntegral : c.expectFireTolerant)
                    {
                        BEAST_EXPECT(result == tecINVARIANT_FAILED);
                        BEAST_EXPECT(messages.contains("Loan interest due is negative"));
                    }
                    else
                    {
                        // Other invariants may still fire (e.g. the
                        // broker-existence check on this raw-inserted loan), so
                        // only assert the specific message is absent.
                        BEAST_EXPECT(!messages.contains("Loan interest due is negative"));
                    }
                }
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

        auto const amendments = all_ | fixCleanup3_3_0;

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
    run() override
    {
        testXRPNotCreated();
        testAccountRootsNotRemoved();
        testAccountRootsDeletedClean();
        testTypesMatch();
        testXRPBalanceCheck();
        testTransactionFeeCheck();
        testNoBadOffers();
        testValidNewAccountRoot();
        testNoModifiedUnmodifiableFields();
        testInvariantOverwrite(all_);
        testInvariantOverwrite(all_ - fixCleanup3_1_3);
        testObjectHasPseudoAccount();
        testSponsorship();
        testTxCheckException();
        testTxCheckFinalizeFalse();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsMisc, app, xrpl);

}  // namespace xrpl::test
