//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Ripple Labs Inc.

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
#include <xrpld/app/tx/detail/ApplyContext.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class FixNFTokenPageLinks_test : public beast::unit_test::suite
{
    // Helper function that returns the number of nfts owned by an account.
    static std::uint32_t
    nftCount(test::jtx::Env& env, test::jtx::Account const& acct)
    {
        Json::Value params;
        params[jss::account] = acct.human();
        params[jss::type] = "state";
        Json::Value nfts = env.rpc("json", "account_nfts", to_string(params));
        return nfts[jss::result][jss::account_nfts].size();
    };

    // A helper function that generates 96 nfts packed into three pages
    // of 32 each.  Returns a sorted vector of the NFTokenIDs packed into
    // the pages.
    std::vector<uint256>
    genPackedTokens(test::jtx::Env& env, test::jtx::Account const& owner)
    {
        using namespace test::jtx;

        std::vector<uint256> nfts;
        nfts.reserve(96);

        // We want to create fully packed NFT pages.  This is a little
        // tricky since the system currently in place is inclined to
        // assign consecutive tokens to only 16 entries per page.
        //
        // By manipulating the internal form of the taxon we can force
        // creation of NFT pages that are completely full.  This lambda
        // tells us the taxon value we should pass in in order for the
        // internal representation to match the passed in value.
        auto internalTaxon = [this, &env](
                                 Account const& acct,
                                 std::uint32_t taxon) -> std::uint32_t {
            std::uint32_t tokenSeq = [this, &env, &acct]() {
                auto const le = env.le(acct);
                if (BEAST_EXPECT(le))
                    return le->at(~sfMintedNFTokens).value_or(0u);
                return 0u;
            }();

            // If fixNFTokenRemint amendment is on, we must
            // add FirstNFTokenSequence.
            if (env.current()->rules().enabled(fixNFTokenRemint))
                tokenSeq += env.le(acct)
                                ->at(~sfFirstNFTokenSequence)
                                .value_or(env.seq(acct));

            return toUInt32(nft::cipheredTaxon(tokenSeq, nft::toTaxon(taxon)));
        };

        for (std::uint32_t i = 0; i < 96; ++i)
        {
            // In order to fill the pages we use the taxon to break them
            // into groups of 16 entries.  By having the internal
            // representation of the taxon go...
            //   0, 3, 2, 5, 4, 7...
            // in sets of 16 NFTs we can get each page to be fully
            // populated.
            std::uint32_t const intTaxon = (i / 16) + (i & 0b10000 ? 2 : 0);
            uint32_t const extTaxon = internalTaxon(owner, intTaxon);
            nfts.push_back(
                token::getNextID(env, owner, extTaxon, tfTransferable));
            env(token::mint(owner, extTaxon), txflags(tfTransferable));
            env.close();
        }

        // Sort the NFTs so they are listed in storage order, not
        // creation order.
        std::sort(nfts.begin(), nfts.end());

        // Verify that the owner does indeed have exactly three pages
        // of NFTs with 32 entries in each page.
        {
            Json::Value params;
            params[jss::account] = owner.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));

            Json::Value const& acctObjs =
                resp[jss::result][jss::account_objects];

            int pageCount = 0;
            for (Json::UInt i = 0; i < acctObjs.size(); ++i)
            {
                if (BEAST_EXPECT(
                        acctObjs[i].isMember(sfNFTokens.jsonName) &&
                        acctObjs[i][sfNFTokens.jsonName].isArray()))
                {
                    BEAST_EXPECT(acctObjs[i][sfNFTokens.jsonName].size() == 32);
                    ++pageCount;
                }
            }
            // If this check fails then the internal NFT directory logic
            // has changed.
            BEAST_EXPECT(pageCount == 3);
        }
        return nfts;
    };

    void
    testLedgerStateFixErrors()
    {
        testcase("LedgerStateFix error cases");

        using namespace test::jtx;

        Account const alice("alice");

        {
            // Verify that the LedgerStateFix transaction is disabled
            // without the fixNFTokenPageLinks amendment.
            Env env{*this, supported_amendments() - fixNFTokenPageLinks};
            env.fund(XRP(1000), alice);

            auto const linkFixFee = drops(env.current()->fees().increment);
            env(ledgerStateFix::nftPageLinks(alice, alice),
                fee(linkFixFee),
                ter(temDISABLED));
        }

        Env env{*this, supported_amendments()};
        env.fund(XRP(1000), alice);
        std::uint32_t const ticketSeq = env.seq(alice);
        env(ticket::create(alice, 1));

        // Preflight

        {
            // Fail preflight1.  Can't combine AcccountTxnID and ticket.
            Json::Value tx = ledgerStateFix::nftPageLinks(alice, alice);
            tx[sfAccountTxnID.jsonName] =
                "00000000000000000000000000000000"
                "00000000000000000000000000000000";
            env(tx, ticket::use(ticketSeq), ter(temINVALID));
        }
        // Fee too low.
        env(ledgerStateFix::nftPageLinks(alice, alice), ter(telINSUF_FEE_P));

        // Invalid flags.
        auto const linkFixFee = drops(env.current()->fees().increment);
        env(ledgerStateFix::nftPageLinks(alice, alice),
            fee(linkFixFee),
            txflags(tfPassive),
            ter(temINVALID_FLAG));

        {
            // ledgerStateFix::nftPageLinks requires an Owner field.
            Json::Value tx = ledgerStateFix::nftPageLinks(alice, alice);
            tx.removeMember(sfOwner.jsonName);
            env(tx, fee(linkFixFee), ter(temINVALID));
        }
        {
            // Invalid LedgerFixType codes.
            Json::Value tx = ledgerStateFix::nftPageLinks(alice, alice);
            tx[sfLedgerFixType.jsonName] = 0;
            env(tx, fee(linkFixFee), ter(tefINVALID_LEDGER_FIX_TYPE));

            tx[sfLedgerFixType.jsonName] = 200;
            env(tx, fee(linkFixFee), ter(tefINVALID_LEDGER_FIX_TYPE));
        }

        // Preclaim
        Account const carol("carol");
        env.memoize(carol);
        env(ledgerStateFix::nftPageLinks(alice, carol),
            fee(linkFixFee),
            ter(tecOBJECT_NOT_FOUND));
    }

    void
    testTokenPageLinkErrors()
    {
        testcase("NFTokenPageLinkFix error cases");

        using namespace test::jtx;

        Account const alice("alice");

        Env env{*this, supported_amendments()};
        env.fund(XRP(1000), alice);

        // These cases all return the same TER code, but they exercise
        // different cases where there is nothing to fix in an owner's
        // NFToken pages.  So they increase test coverage.

        // Owner has no pages to fix.
        auto const linkFixFee = drops(env.current()->fees().increment);
        env(ledgerStateFix::nftPageLinks(alice, alice),
            fee(linkFixFee),
            ter(tecFAILED_PROCESSING));

        // Alice has only one page.
        env(token::mint(alice), txflags(tfTransferable));
        env.close();

        env(ledgerStateFix::nftPageLinks(alice, alice),
            fee(linkFixFee),
            ter(tecFAILED_PROCESSING));

        // Alice has at least three pages.
        for (std::uint32_t i = 0; i < 64; ++i)
        {
            env(token::mint(alice), txflags(tfTransferable));
            env.close();
        }

        env(ledgerStateFix::nftPageLinks(alice, alice),
            fee(linkFixFee),
            ter(tecFAILED_PROCESSING));
    }

    void
    testFixNFTokenPageLinks()
    {
        // Steps:
        // 1. Before the fixNFTokenPageLinks amendment is enabled, build the
        //    three kinds of damaged NFToken directories we know about:
        //     A. One where there is only one page, but without the final index.
        //     B. One with multiple pages and a missing final page.
        //     C. One with links missing in the middle of the chain.
        // 2. Enable the fixNFTokenPageLinks amendment.
        // 3. Invoke the LedgerStateFix transactor and repair the directories.
        testcase("Fix links");

        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const daria("daria");

        Env env{*this, supported_amendments() - fixNFTokenPageLinks};
        env.fund(XRP(1000), alice, bob, carol, daria);

        //**********************************************************************
        // Step 1A: Create damaged NFToken directories:
        //   o One where there is only one page, but without the final index.
        //**********************************************************************

        // alice generates three packed pages.
        std::vector<uint256> aliceNFTs = genPackedTokens(env, alice);
        BEAST_EXPECT(nftCount(env, alice) == 96);
        BEAST_EXPECT(ownerCount(env, alice) == 3);

        // Get the index of the middle page.
        uint256 const aliceMiddleNFTokenPageIndex = [&env, &alice]() {
            auto lastNFTokenPage = env.le(keylet::nftpage_max(alice));
            return lastNFTokenPage->at(sfPreviousPageMin);
        }();

        // alice burns all the tokens in the first and last pages.
        for (int i = 0; i < 32; ++i)
        {
            env(token::burn(alice, {aliceNFTs[i]}));
            env.close();
        }
        aliceNFTs.erase(aliceNFTs.begin(), aliceNFTs.begin() + 32);
        for (int i = 0; i < 32; ++i)
        {
            env(token::burn(alice, {aliceNFTs.back()}));
            aliceNFTs.pop_back();
            env.close();
        }
        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(nftCount(env, alice) == 32);

        // Removing the last token from the last page deletes the last
        // page.  This is a bug.  The contents of the next-to-last page
        // should have been moved into the last page.
        BEAST_EXPECT(!env.le(keylet::nftpage_max(alice)));

        // alice's "middle" page is still present, but has no links.
        {
            auto aliceMiddleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), aliceMiddleNFTokenPageIndex));
            if (!BEAST_EXPECT(aliceMiddleNFTokenPage))
                return;

            BEAST_EXPECT(
                !aliceMiddleNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(
                !aliceMiddleNFTokenPage->isFieldPresent(sfNextPageMin));
        }

        //**********************************************************************
        // Step 1B: Create damaged NFToken directories:
        //   o One with multiple pages and a missing final page.
        //**********************************************************************

        // bob generates three packed pages.
        std::vector<uint256> bobNFTs = genPackedTokens(env, bob);
        BEAST_EXPECT(nftCount(env, bob) == 96);
        BEAST_EXPECT(ownerCount(env, bob) == 3);

        // Get the index of the middle page.
        uint256 const bobMiddleNFTokenPageIndex = [&env, &bob]() {
            auto lastNFTokenPage = env.le(keylet::nftpage_max(bob));
            return lastNFTokenPage->at(sfPreviousPageMin);
        }();

        // bob burns all the tokens in the very last page.
        for (int i = 0; i < 32; ++i)
        {
            env(token::burn(bob, {bobNFTs.back()}));
            bobNFTs.pop_back();
            env.close();
        }
        BEAST_EXPECT(nftCount(env, bob) == 64);
        BEAST_EXPECT(ownerCount(env, bob) == 2);

        // Removing the last token from the last page deletes the last
        // page.  This is a bug.  The contents of the next-to-last page
        // should have been moved into the last page.
        BEAST_EXPECT(!env.le(keylet::nftpage_max(bob)));

        // bob's "middle" page is still present, but has lost the
        // NextPageMin field.
        {
            auto bobMiddleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(bob), bobMiddleNFTokenPageIndex));
            if (!BEAST_EXPECT(bobMiddleNFTokenPage))
                return;

            BEAST_EXPECT(
                bobMiddleNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(!bobMiddleNFTokenPage->isFieldPresent(sfNextPageMin));
        }

        //**********************************************************************
        // Step 1C: Create damaged NFToken directories:
        //   o One with links missing in the middle of the chain.
        //**********************************************************************

        // carol generates three packed pages.
        std::vector<uint256> carolNFTs = genPackedTokens(env, carol);
        BEAST_EXPECT(nftCount(env, carol) == 96);
        BEAST_EXPECT(ownerCount(env, carol) == 3);

        // Get the index of the middle page.
        uint256 const carolMiddleNFTokenPageIndex = [&env, &carol]() {
            auto lastNFTokenPage = env.le(keylet::nftpage_max(carol));
            return lastNFTokenPage->at(sfPreviousPageMin);
        }();

        // carol sells all of the tokens in the very last page to daria.
        std::vector<uint256> dariaNFTs;
        dariaNFTs.reserve(32);
        for (int i = 0; i < 32; ++i)
        {
            uint256 const offerIndex =
                keylet::nftoffer(carol, env.seq(carol)).key;
            env(token::createOffer(carol, carolNFTs.back(), XRP(0)),
                txflags(tfSellNFToken));
            env.close();

            env(token::acceptSellOffer(daria, offerIndex));
            env.close();

            dariaNFTs.push_back(carolNFTs.back());
            carolNFTs.pop_back();
        }
        BEAST_EXPECT(nftCount(env, carol) == 64);
        BEAST_EXPECT(ownerCount(env, carol) == 2);

        // Removing the last token from the last page deletes the last
        // page.  This is a bug.  The contents of the next-to-last page
        // should have been moved into the last page.
        BEAST_EXPECT(!env.le(keylet::nftpage_max(carol)));

        // carol's "middle" page is still present, but has lost the
        // NextPageMin field.
        auto carolMiddleNFTokenPage = env.le(keylet::nftpage(
            keylet::nftpage_min(carol), carolMiddleNFTokenPageIndex));
        if (!BEAST_EXPECT(carolMiddleNFTokenPage))
            return;

        BEAST_EXPECT(carolMiddleNFTokenPage->isFieldPresent(sfPreviousPageMin));
        BEAST_EXPECT(!carolMiddleNFTokenPage->isFieldPresent(sfNextPageMin));

        // At this point carol's NFT directory has the same problem that
        // bob's has: the last page is missing.  Now we make things more
        // complicated by putting the last page back.  carol buys their NFTs
        // back from daria.
        for (uint256 const& nft : dariaNFTs)
        {
            uint256 const offerIndex =
                keylet::nftoffer(carol, env.seq(carol)).key;
            env(token::createOffer(carol, nft, drops(1)), token::owner(daria));
            env.close();

            env(token::acceptBuyOffer(daria, offerIndex));
            env.close();

            carolNFTs.push_back(nft);
        }

        // Note that carol actually owns 96 NFTs, but only 64 are reported
        // because the links are damaged.
        BEAST_EXPECT(nftCount(env, carol) == 64);
        BEAST_EXPECT(ownerCount(env, carol) == 3);

        // carol's "middle" page is present and still has no NextPageMin field.
        {
            auto carolMiddleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(carol), carolMiddleNFTokenPageIndex));
            if (!BEAST_EXPECT(carolMiddleNFTokenPage))
                return;

            BEAST_EXPECT(
                carolMiddleNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(
                !carolMiddleNFTokenPage->isFieldPresent(sfNextPageMin));
        }
        // carol has a "last" page again, but it has no PreviousPageMin field.
        {
            auto carolLastNFTokenPage = env.le(keylet::nftpage_max(carol));

            BEAST_EXPECT(
                !carolLastNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(!carolLastNFTokenPage->isFieldPresent(sfNextPageMin));
        }

        //**********************************************************************
        // Step 2: Enable the fixNFTokenPageLinks amendment.
        //**********************************************************************
        // Verify that the LedgerStateFix transaction is not enabled.
        auto const linkFixFee = drops(env.current()->fees().increment);
        env(ledgerStateFix::nftPageLinks(daria, alice),
            fee(linkFixFee),
            ter(temDISABLED));

        // Wait 15 ledgers so the LedgerStateFix transaction is no longer
        // retried.
        for (int i = 0; i < 15; ++i)
            env.close();

        env.enableFeature(fixNFTokenPageLinks);
        env.close();

        //**********************************************************************
        // Step 3A: Repair the one-page directory (alice's)
        //**********************************************************************

        // Verify that alice's NFToken directory is still damaged.

        // alice's last page should still be missing.
        BEAST_EXPECT(!env.le(keylet::nftpage_max(alice)));

        // alice's "middle" page is still present and has no links.
        {
            auto aliceMiddleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), aliceMiddleNFTokenPageIndex));
            if (!BEAST_EXPECT(aliceMiddleNFTokenPage))
                return;

            BEAST_EXPECT(
                !aliceMiddleNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(
                !aliceMiddleNFTokenPage->isFieldPresent(sfNextPageMin));
        }

        // The server "remembers" daria's failed nftPageLinks transaction
        // signature.  So we need to advance daria's sequence number before
        // daria can submit a similar transaction.
        env(noop(daria));

        // daria fixes the links in alice's NFToken directory.
        env(ledgerStateFix::nftPageLinks(daria, alice), fee(linkFixFee));
        env.close();

        // alices's last page should now be present and include no links.
        {
            auto aliceLastNFTokenPage = env.le(keylet::nftpage_max(alice));
            if (!BEAST_EXPECT(aliceLastNFTokenPage))
                return;

            BEAST_EXPECT(
                !aliceLastNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(!aliceLastNFTokenPage->isFieldPresent(sfNextPageMin));
        }

        // alice's middle page should be gone.
        BEAST_EXPECT(!env.le(keylet::nftpage(
            keylet::nftpage_min(alice), aliceMiddleNFTokenPageIndex)));

        BEAST_EXPECT(nftCount(env, alice) == 32);
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        //**********************************************************************
        // Step 3B: Repair the two-page directory (bob's)
        //**********************************************************************

        // Verify that bob's NFToken directory is still damaged.

        // bob's last page should still be missing.
        BEAST_EXPECT(!env.le(keylet::nftpage_max(bob)));

        // bob's "middle" page is still present and missing NextPageMin.
        {
            auto bobMiddleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(bob), bobMiddleNFTokenPageIndex));
            if (!BEAST_EXPECT(bobMiddleNFTokenPage))
                return;

            BEAST_EXPECT(
                bobMiddleNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(!bobMiddleNFTokenPage->isFieldPresent(sfNextPageMin));
        }

        // daria fixes the links in bob's NFToken directory.
        env(ledgerStateFix::nftPageLinks(daria, bob), fee(linkFixFee));
        env.close();

        // bob's last page should now be present and include a previous
        // link but no next link.
        {
            auto const lastPageKeylet = keylet::nftpage_max(bob);
            auto const bobLastNFTokenPage = env.le(lastPageKeylet);
            if (!BEAST_EXPECT(bobLastNFTokenPage))
                return;

            BEAST_EXPECT(bobLastNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(
                bobLastNFTokenPage->at(sfPreviousPageMin) !=
                bobMiddleNFTokenPageIndex);
            BEAST_EXPECT(!bobLastNFTokenPage->isFieldPresent(sfNextPageMin));

            auto const bobNewFirstNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(bob),
                bobLastNFTokenPage->at(sfPreviousPageMin)));
            if (!BEAST_EXPECT(bobNewFirstNFTokenPage))
                return;

            BEAST_EXPECT(
                bobNewFirstNFTokenPage->isFieldPresent(sfNextPageMin) &&
                bobNewFirstNFTokenPage->at(sfNextPageMin) ==
                    lastPageKeylet.key);
            BEAST_EXPECT(
                !bobNewFirstNFTokenPage->isFieldPresent(sfPreviousPageMin));
        }

        // bob's middle page should be gone.
        BEAST_EXPECT(!env.le(keylet::nftpage(
            keylet::nftpage_min(bob), bobMiddleNFTokenPageIndex)));

        BEAST_EXPECT(nftCount(env, bob) == 64);
        BEAST_EXPECT(ownerCount(env, bob) == 2);

        //**********************************************************************
        // Step 3C: Repair the three-page directory (carol's)
        //**********************************************************************

        // Verify that carol's NFToken directory is still damaged.

        // carol's "middle" page is present and has no NextPageMin field.
        {
            auto carolMiddleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(carol), carolMiddleNFTokenPageIndex));
            if (!BEAST_EXPECT(carolMiddleNFTokenPage))
                return;

            BEAST_EXPECT(
                carolMiddleNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(
                !carolMiddleNFTokenPage->isFieldPresent(sfNextPageMin));
        }
        // carol has a "last" page, but it has no PreviousPageMin field.
        {
            auto carolLastNFTokenPage = env.le(keylet::nftpage_max(carol));

            BEAST_EXPECT(
                !carolLastNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(!carolLastNFTokenPage->isFieldPresent(sfNextPageMin));
        }

        // carol fixes the links in their own NFToken directory.
        env(ledgerStateFix::nftPageLinks(carol, carol), fee(linkFixFee));
        env.close();

        {
            // carol's "middle" page is present and now has a NextPageMin field.
            auto const lastPageKeylet = keylet::nftpage_max(carol);
            auto carolMiddleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(carol), carolMiddleNFTokenPageIndex));
            if (!BEAST_EXPECT(carolMiddleNFTokenPage))
                return;

            BEAST_EXPECT(
                carolMiddleNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(
                carolMiddleNFTokenPage->isFieldPresent(sfNextPageMin) &&
                carolMiddleNFTokenPage->at(sfNextPageMin) ==
                    lastPageKeylet.key);

            // carol has a "last" page that includes a PreviousPageMin field.
            auto carolLastNFTokenPage = env.le(lastPageKeylet);
            if (!BEAST_EXPECT(carolLastNFTokenPage))
                return;

            BEAST_EXPECT(
                carolLastNFTokenPage->isFieldPresent(sfPreviousPageMin) &&
                carolLastNFTokenPage->at(sfPreviousPageMin) ==
                    carolMiddleNFTokenPageIndex);
            BEAST_EXPECT(!carolLastNFTokenPage->isFieldPresent(sfNextPageMin));

            // carol also has a "first" page that includes a NextPageMin field.
            auto carolFirstNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(carol),
                carolMiddleNFTokenPage->at(sfPreviousPageMin)));
            if (!BEAST_EXPECT(carolFirstNFTokenPage))
                return;

            BEAST_EXPECT(
                carolFirstNFTokenPage->isFieldPresent(sfNextPageMin) &&
                carolFirstNFTokenPage->at(sfNextPageMin) ==
                    carolMiddleNFTokenPageIndex);
            BEAST_EXPECT(
                !carolFirstNFTokenPage->isFieldPresent(sfPreviousPageMin));
        }

        // With the link repair, the server knows that carol has 96 NFTs.
        BEAST_EXPECT(nftCount(env, carol) == 96);
        BEAST_EXPECT(ownerCount(env, carol) == 3);
    }

public:
    void
    run() override
    {
        testLedgerStateFixErrors();
        testTokenPageLinkErrors();
        testFixNFTokenPageLinks();
    }
};

BEAST_DEFINE_TESTSUITE(FixNFTokenPageLinks, tx, ripple);

}  // namespace ripple
