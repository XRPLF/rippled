#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/ApplyContext.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsEscrowNFT_test : public InvariantsBase
{
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
                sleNew->setFieldU64(sfOutstandingAmount, std::numeric_limits<std::uint64_t>::max());
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
                sleNew->setFieldU64(sfLockedAmount, std::numeric_limits<std::uint64_t>::max());
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
                sleNew->setFieldU64(sfMPTAmount, std::numeric_limits<std::uint64_t>::max());
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
                sleNew->setFieldU64(sfLockedAmount, std::numeric_limits<std::uint64_t>::max());
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testNFTokenPageInvariants()
    {
        using namespace test::jtx;
        testcase << "NFTokenPage";

        // lambda that returns an STArray of NFTokenIDs.
        UInt256 const firstNFTID(
            "0000000000000000000000000000000000000001FFFFFFFFFFFFFFFF00000000");
        auto makeNFTokenIDs = [&firstNFTID](unsigned int nftCount) {
            SOTemplate const* nfTokenTemplate =
                InnerObjectFormats::getInstance().findSOTemplateBySField(sfNFToken);

            UInt256 nftID(firstNFTID);
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
    run() override
    {
        testNoZeroEscrow();
        testNFTokenPageInvariants();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsEscrowNFT, app, xrpl);

}  // namespace xrpl::test
