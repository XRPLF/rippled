//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2021 Ripple Labs Inc.

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
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

#include <random>

namespace ripple {

class NFTokenBurnBaseUtil_test : public beast::unit_test::suite
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

    // Helper function that returns new nft id for an account and create
    // specified number of sell offers
    uint256
    createNftAndOffers(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        std::vector<uint256>& offerIndexes,
        size_t const tokenCancelCount)
    {
        using namespace test::jtx;
        uint256 const nftokenID =
            token::getNextID(env, owner, 0, tfTransferable);
        env(token::mint(owner, 0),
            token::uri(std::string(maxTokenURILength, 'u')),
            txflags(tfTransferable));
        env.close();

        offerIndexes.reserve(tokenCancelCount);

        for (uint32_t i = 0; i < tokenCancelCount; ++i)
        {
            // Create sell offer
            offerIndexes.push_back(keylet::nftoffer(owner, env.seq(owner)).key);
            env(token::createOffer(owner, nftokenID, drops(1)),
                txflags(tfSellNFToken));
            env.close();
        }

        return nftokenID;
    };

    // printNFTPages is a helper function that may be used for debugging.
    //
    // It uses the ledger RPC command to show the NFT pages in the ledger.
    // This parameter controls how noisy the output is.
    enum Volume : bool {
        quiet = false,
        noisy = true,
    };

    void
    printNFTPages(test::jtx::Env& env, Volume vol)
    {
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::binary] = false;
        {
            Json::Value jrr = env.rpc(
                "json",
                "ledger_data",
                boost::lexical_cast<std::string>(jvParams));

            // Iterate the state and print all NFTokenPages.
            if (!jrr.isMember(jss::result) ||
                !jrr[jss::result].isMember(jss::state))
            {
                std::cout << "No ledger state found!" << std::endl;
                return;
            }
            Json::Value& state = jrr[jss::result][jss::state];
            if (!state.isArray())
            {
                std::cout << "Ledger state is not array!" << std::endl;
                return;
            }
            for (Json::UInt i = 0; i < state.size(); ++i)
            {
                if (state[i].isMember(sfNFTokens.jsonName) &&
                    state[i][sfNFTokens.jsonName].isArray())
                {
                    std::uint32_t tokenCount =
                        state[i][sfNFTokens.jsonName].size();
                    std::cout << tokenCount << " NFtokens in page "
                              << state[i][jss::index].asString() << std::endl;

                    if (vol == noisy)
                    {
                        std::cout << state[i].toStyledString() << std::endl;
                    }
                    else
                    {
                        if (tokenCount > 0)
                            std::cout << "first: "
                                      << state[i][sfNFTokens.jsonName][0u]
                                             .toStyledString()
                                      << std::endl;
                        if (tokenCount > 1)
                            std::cout
                                << "last: "
                                << state[i][sfNFTokens.jsonName][tokenCount - 1]
                                       .toStyledString()
                                << std::endl;
                    }
                }
            }
        }
    }

    void
    testBurnRandom(FeatureBitset features)
    {
        // Exercise a number of conditions with NFT burning.
        testcase("Burn random");

        using namespace test::jtx;

        Env env{*this, features};

        // Keep information associated with each account together.
        struct AcctStat
        {
            test::jtx::Account const acct;
            std::vector<uint256> nfts;

            AcctStat(char const* name) : acct(name)
            {
            }

            operator test::jtx::Account() const
            {
                return acct;
            }
        };
        AcctStat alice{"alice"};
        AcctStat becky{"becky"};
        AcctStat minter{"minter"};

        env.fund(XRP(10000), alice, becky, minter);
        env.close();

        // Both alice and minter mint nfts in case that makes any difference.
        env(token::setMinter(alice, minter));
        env.close();

        // Create enough NFTs that alice, becky, and minter can all have
        // at least three pages of NFTs.  This will cause more activity in
        // the page coalescing code.  If we make 210 NFTs in total, we can
        // have alice and minter each make 105.  That will allow us to
        // distribute 70 NFTs to our three participants.
        //
        // Give each NFT a pseudo-randomly chosen fee so the NFTs are
        // distributed pseudo-randomly through the pages.  This should
        // prevent alice's and minter's NFTs from clustering together
        // in becky's directory.
        //
        // Use a default initialized mercenne_twister because we want the
        // effect of random numbers, but we want the test to run the same
        // way each time.
        std::mt19937 engine;
        std::uniform_int_distribution<std::size_t> feeDist(
            decltype(maxTransferFee){}, maxTransferFee);

        alice.nfts.reserve(105);
        while (alice.nfts.size() < 105)
        {
            std::uint16_t const xferFee = feeDist(engine);
            alice.nfts.push_back(token::getNextID(
                env, alice, 0u, tfTransferable | tfBurnable, xferFee));
            env(token::mint(alice),
                txflags(tfTransferable | tfBurnable),
                token::xferFee(xferFee));
            env.close();
        }

        minter.nfts.reserve(105);
        while (minter.nfts.size() < 105)
        {
            std::uint16_t const xferFee = feeDist(engine);
            minter.nfts.push_back(token::getNextID(
                env, alice, 0u, tfTransferable | tfBurnable, xferFee));
            env(token::mint(minter),
                txflags(tfTransferable | tfBurnable),
                token::xferFee(xferFee),
                token::issuer(alice));
            env.close();
        }

        // All of the NFTs are now minted.  Transfer 35 each over to becky so
        // we end up with 70 NFTs in each account.
        becky.nfts.reserve(70);
        {
            auto aliceIter = alice.nfts.begin();
            auto minterIter = minter.nfts.begin();
            while (becky.nfts.size() < 70)
            {
                // We do the same work on alice and minter, so make a lambda.
                auto xferNFT = [&env, &becky](AcctStat& acct, auto& iter) {
                    uint256 offerIndex =
                        keylet::nftoffer(acct.acct, env.seq(acct.acct)).key;
                    env(token::createOffer(acct, *iter, XRP(0)),
                        txflags(tfSellNFToken));
                    env.close();
                    env(token::acceptSellOffer(becky, offerIndex));
                    env.close();
                    becky.nfts.push_back(*iter);
                    iter = acct.nfts.erase(iter);
                    iter += 2;
                };
                xferNFT(alice, aliceIter);
                xferNFT(minter, minterIter);
            }
            BEAST_EXPECT(aliceIter == alice.nfts.end());
            BEAST_EXPECT(minterIter == minter.nfts.end());
        }

        // Now all three participants have 70 NFTs.
        BEAST_EXPECT(nftCount(env, alice.acct) == 70);
        BEAST_EXPECT(nftCount(env, becky.acct) == 70);
        BEAST_EXPECT(nftCount(env, minter.acct) == 70);

        // Next we'll create offers for all of those NFTs.  This calls for
        // another lambda.
        auto addOffers =
            [&env](AcctStat& owner, AcctStat& other1, AcctStat& other2) {
                for (uint256 nft : owner.nfts)
                {
                    // Create sell offers for owner.
                    env(token::createOffer(owner, nft, drops(1)),
                        txflags(tfSellNFToken),
                        token::destination(other1));
                    env(token::createOffer(owner, nft, drops(1)),
                        txflags(tfSellNFToken),
                        token::destination(other2));
                    env.close();

                    // Create buy offers for other1 and other2.
                    env(token::createOffer(other1, nft, drops(1)),
                        token::owner(owner));
                    env(token::createOffer(other2, nft, drops(1)),
                        token::owner(owner));
                    env.close();

                    env(token::createOffer(other2, nft, drops(2)),
                        token::owner(owner));
                    env(token::createOffer(other1, nft, drops(2)),
                        token::owner(owner));
                    env.close();
                }
            };
        addOffers(alice, becky, minter);
        addOffers(becky, minter, alice);
        addOffers(minter, alice, becky);
        BEAST_EXPECT(ownerCount(env, alice) == 424);
        BEAST_EXPECT(ownerCount(env, becky) == 424);
        BEAST_EXPECT(ownerCount(env, minter) == 424);

        // Now each of the 270 NFTs has six offers associated with it.
        // Randomly select an NFT out of the pile and burn it.  Continue
        // the process until all NFTs are burned.
        AcctStat* const stats[3] = {&alice, &becky, &minter};
        std::uniform_int_distribution<std::size_t> acctDist(0, 2);
        std::uniform_int_distribution<std::size_t> mintDist(0, 1);

        while (stats[0]->nfts.size() > 0 || stats[1]->nfts.size() > 0 ||
               stats[2]->nfts.size() > 0)
        {
            // Pick an account to burn an nft.  If there are no nfts left
            // pick again.
            AcctStat& owner = *(stats[acctDist(engine)]);
            if (owner.nfts.empty())
                continue;

            // Pick one of the nfts.
            std::uniform_int_distribution<std::size_t> nftDist(
                0lu, owner.nfts.size() - 1);
            auto nftIter = owner.nfts.begin() + nftDist(engine);
            uint256 const nft = *nftIter;
            owner.nfts.erase(nftIter);

            // Decide which of the accounts should burn the nft.  If the
            // owner is becky then any of the three accounts can burn.
            // Otherwise either alice or minter can burn.
            AcctStat& burner = owner.acct == becky.acct
                ? *(stats[acctDist(engine)])
                : mintDist(engine) ? alice
                                   : minter;

            if (owner.acct == burner.acct)
                env(token::burn(burner, nft));
            else
                env(token::burn(burner, nft), token::owner(owner));
            env.close();

            // Every time we burn an nft, the number of nfts they hold should
            // match the number of nfts we think they hold.
            BEAST_EXPECT(nftCount(env, alice.acct) == alice.nfts.size());
            BEAST_EXPECT(nftCount(env, becky.acct) == becky.nfts.size());
            BEAST_EXPECT(nftCount(env, minter.acct) == minter.nfts.size());
        }
        BEAST_EXPECT(nftCount(env, alice.acct) == 0);
        BEAST_EXPECT(nftCount(env, becky.acct) == 0);
        BEAST_EXPECT(nftCount(env, minter.acct) == 0);

        // When all nfts are burned none of the accounts should have
        // an ownerCount.
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, becky) == 0);
        BEAST_EXPECT(ownerCount(env, minter) == 0);
    }

    void
    testBurnSequential(FeatureBitset features)
    {
        // The earlier burn test randomizes which nft is burned.  There are
        // a couple of directory merging scenarios that can only be tested by
        // inserting and deleting in an ordered fashion.  We do that testing
        // now.
        testcase("Burn sequential");

        using namespace test::jtx;

        Account const alice{"alice"};

        Env env{*this, features};
        env.fund(XRP(1000), alice);

        // A lambda that generates 96 nfts packed into three pages of 32 each.
        // Returns a sorted vector of the NFTokenIDs packed into the pages.
        auto genPackedTokens = [this, &env, &alice]() {
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
            auto internalTaxon = [&env](
                                     Account const& acct,
                                     std::uint32_t taxon) -> std::uint32_t {
                std::uint32_t tokenSeq =
                    env.le(acct)->at(~sfMintedNFTokens).value_or(0);

                // If fixNFTokenRemint amendment is on, we must
                // add FirstNFTokenSequence.
                if (env.current()->rules().enabled(fixNFTokenRemint))
                    tokenSeq += env.le(acct)
                                    ->at(~sfFirstNFTokenSequence)
                                    .value_or(env.seq(acct));

                return toUInt32(
                    nft::cipheredTaxon(tokenSeq, nft::toTaxon(taxon)));
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
                uint32_t const extTaxon = internalTaxon(alice, intTaxon);
                nfts.push_back(token::getNextID(env, alice, extTaxon));
                env(token::mint(alice, extTaxon));
                env.close();
            }

            // Sort the NFTs so they are listed in storage order, not
            // creation order.
            std::sort(nfts.begin(), nfts.end());

            // Verify that the ledger does indeed contain exactly three pages
            // of NFTs with 32 entries in each page.
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "current";
            jvParams[jss::binary] = false;
            {
                Json::Value jrr = env.rpc(
                    "json",
                    "ledger_data",
                    boost::lexical_cast<std::string>(jvParams));

                Json::Value& state = jrr[jss::result][jss::state];

                int pageCount = 0;
                for (Json::UInt i = 0; i < state.size(); ++i)
                {
                    if (state[i].isMember(sfNFTokens.jsonName) &&
                        state[i][sfNFTokens.jsonName].isArray())
                    {
                        BEAST_EXPECT(
                            state[i][sfNFTokens.jsonName].size() == 32);
                        ++pageCount;
                    }
                }
                // If this check fails then the internal NFT directory logic
                // has changed.
                BEAST_EXPECT(pageCount == 3);
            }
            return nfts;
        };
        {
            // Generate three packed pages.  Then burn the tokens in order from
            // first to last.  This exercises specific cases where coalescing
            // pages is not possible.
            std::vector<uint256> nfts = genPackedTokens();
            BEAST_EXPECT(nftCount(env, alice) == 96);
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            for (uint256 const& nft : nfts)
            {
                env(token::burn(alice, {nft}));
                env.close();
            }
            BEAST_EXPECT(nftCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // A lambda verifies that the ledger no longer contains any NFT pages.
        auto checkNoTokenPages = [this, &env]() {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "current";
            jvParams[jss::binary] = false;
            {
                Json::Value jrr = env.rpc(
                    "json",
                    "ledger_data",
                    boost::lexical_cast<std::string>(jvParams));

                Json::Value& state = jrr[jss::result][jss::state];

                for (Json::UInt i = 0; i < state.size(); ++i)
                {
                    BEAST_EXPECT(!state[i].isMember(sfNFTokens.jsonName));
                }
            }
        };
        checkNoTokenPages();
        {
            // Generate three packed pages.  Then burn the tokens in order from
            // last to first.  This exercises different specific cases where
            // coalescing pages is not possible.
            std::vector<uint256> nfts = genPackedTokens();
            BEAST_EXPECT(nftCount(env, alice) == 96);
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            // Verify that that all three pages are present and remember the
            // indexes.
            auto lastNFTokenPage = env.le(keylet::nftpage_max(alice));
            if (!BEAST_EXPECT(lastNFTokenPage))
                return;

            uint256 const middleNFTokenPageIndex =
                lastNFTokenPage->at(sfPreviousPageMin);
            auto middleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), middleNFTokenPageIndex));
            if (!BEAST_EXPECT(middleNFTokenPage))
                return;

            uint256 const firstNFTokenPageIndex =
                middleNFTokenPage->at(sfPreviousPageMin);
            auto firstNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), firstNFTokenPageIndex));
            if (!BEAST_EXPECT(firstNFTokenPage))
                return;

            // Burn almost all the tokens in the very last page.
            for (int i = 0; i < 31; ++i)
            {
                env(token::burn(alice, {nfts.back()}));
                nfts.pop_back();
                env.close();
            }

            // Verify that the last page is still present and contains just one
            // NFT.
            lastNFTokenPage = env.le(keylet::nftpage_max(alice));
            if (!BEAST_EXPECT(lastNFTokenPage))
                return;

            BEAST_EXPECT(
                lastNFTokenPage->getFieldArray(sfNFTokens).size() == 1);
            BEAST_EXPECT(lastNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(!lastNFTokenPage->isFieldPresent(sfNextPageMin));

            // Delete the last token from the last page.
            env(token::burn(alice, {nfts.back()}));
            nfts.pop_back();
            env.close();

            if (features[fixNFTokenPageLinks])
            {
                // Removing the last token from the last page deletes the
                // _previous_ page because we need to preserve that last
                // page an an anchor.  The contents of the next-to-last page
                // are moved into the last page.
                lastNFTokenPage = env.le(keylet::nftpage_max(alice));
                BEAST_EXPECT(lastNFTokenPage);
                BEAST_EXPECT(
                    lastNFTokenPage->at(~sfPreviousPageMin) ==
                    firstNFTokenPageIndex);
                BEAST_EXPECT(!lastNFTokenPage->isFieldPresent(sfNextPageMin));
                BEAST_EXPECT(
                    lastNFTokenPage->getFieldArray(sfNFTokens).size() == 32);

                // The "middle" page should be gone.
                middleNFTokenPage = env.le(keylet::nftpage(
                    keylet::nftpage_min(alice), middleNFTokenPageIndex));
                BEAST_EXPECT(!middleNFTokenPage);

                // The "first" page should still be present and linked to
                // the last page.
                firstNFTokenPage = env.le(keylet::nftpage(
                    keylet::nftpage_min(alice), firstNFTokenPageIndex));
                BEAST_EXPECT(firstNFTokenPage);
                BEAST_EXPECT(
                    !firstNFTokenPage->isFieldPresent(sfPreviousPageMin));
                BEAST_EXPECT(
                    firstNFTokenPage->at(~sfNextPageMin) ==
                    lastNFTokenPage->key());
                BEAST_EXPECT(
                    lastNFTokenPage->getFieldArray(sfNFTokens).size() == 32);
            }
            else
            {
                // Removing the last token from the last page deletes the last
                // page.  This is a bug.  The contents of the next-to-last page
                // should have been moved into the last page.
                lastNFTokenPage = env.le(keylet::nftpage_max(alice));
                BEAST_EXPECT(!lastNFTokenPage);

                // The "middle" page is still present, but has lost the
                // NextPageMin field.
                middleNFTokenPage = env.le(keylet::nftpage(
                    keylet::nftpage_min(alice), middleNFTokenPageIndex));
                if (!BEAST_EXPECT(middleNFTokenPage))
                    return;
                BEAST_EXPECT(
                    middleNFTokenPage->isFieldPresent(sfPreviousPageMin));
                BEAST_EXPECT(!middleNFTokenPage->isFieldPresent(sfNextPageMin));
            }

            // Delete the rest of the NFTokens.
            while (!nfts.empty())
            {
                env(token::burn(alice, {nfts.back()}));
                nfts.pop_back();
                env.close();
            }
            BEAST_EXPECT(nftCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
        checkNoTokenPages();
        {
            // Generate three packed pages.  Then burn all tokens in the middle
            // page.  This exercises the case where a page is removed between
            // two fully populated pages.
            std::vector<uint256> nfts = genPackedTokens();
            BEAST_EXPECT(nftCount(env, alice) == 96);
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            // Verify that that all three pages are present and remember the
            // indexes.
            auto lastNFTokenPage = env.le(keylet::nftpage_max(alice));
            if (!BEAST_EXPECT(lastNFTokenPage))
                return;

            uint256 const middleNFTokenPageIndex =
                lastNFTokenPage->at(sfPreviousPageMin);
            auto middleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), middleNFTokenPageIndex));
            if (!BEAST_EXPECT(middleNFTokenPage))
                return;

            uint256 const firstNFTokenPageIndex =
                middleNFTokenPage->at(sfPreviousPageMin);
            auto firstNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), firstNFTokenPageIndex));
            if (!BEAST_EXPECT(firstNFTokenPage))
                return;

            for (std::size_t i = 32; i < 64; ++i)
            {
                env(token::burn(alice, nfts[i]));
                env.close();
            }
            nfts.erase(nfts.begin() + 32, nfts.begin() + 64);
            BEAST_EXPECT(nftCount(env, alice) == 64);
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            // Verify that middle page is gone and the links in the two
            // remaining pages are correct.
            middleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), middleNFTokenPageIndex));
            BEAST_EXPECT(!middleNFTokenPage);

            lastNFTokenPage = env.le(keylet::nftpage_max(alice));
            BEAST_EXPECT(!lastNFTokenPage->isFieldPresent(sfNextPageMin));
            BEAST_EXPECT(
                lastNFTokenPage->getFieldH256(sfPreviousPageMin) ==
                firstNFTokenPageIndex);

            firstNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), firstNFTokenPageIndex));
            BEAST_EXPECT(
                firstNFTokenPage->getFieldH256(sfNextPageMin) ==
                keylet::nftpage_max(alice).key);
            BEAST_EXPECT(!firstNFTokenPage->isFieldPresent(sfPreviousPageMin));

            // Burn the remaining nfts.
            for (uint256 const& nft : nfts)
            {
                env(token::burn(alice, {nft}));
                env.close();
            }
            BEAST_EXPECT(nftCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
        checkNoTokenPages();
        {
            // Generate three packed pages.  Then burn all the tokens in the
            // first page followed by all the tokens in the last page.  This
            // exercises a specific case where coalescing pages is not possible.
            std::vector<uint256> nfts = genPackedTokens();
            BEAST_EXPECT(nftCount(env, alice) == 96);
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            // Verify that that all three pages are present and remember the
            // indexes.
            auto lastNFTokenPage = env.le(keylet::nftpage_max(alice));
            if (!BEAST_EXPECT(lastNFTokenPage))
                return;

            uint256 const middleNFTokenPageIndex =
                lastNFTokenPage->at(sfPreviousPageMin);
            auto middleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), middleNFTokenPageIndex));
            if (!BEAST_EXPECT(middleNFTokenPage))
                return;

            uint256 const firstNFTokenPageIndex =
                middleNFTokenPage->at(sfPreviousPageMin);
            auto firstNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), firstNFTokenPageIndex));
            if (!BEAST_EXPECT(firstNFTokenPage))
                return;

            // Burn all the tokens in the first page.
            std::reverse(nfts.begin(), nfts.end());
            for (int i = 0; i < 32; ++i)
            {
                env(token::burn(alice, {nfts.back()}));
                nfts.pop_back();
                env.close();
            }

            // Verify the first page is gone.
            firstNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), firstNFTokenPageIndex));
            BEAST_EXPECT(!firstNFTokenPage);

            // Check the links in the other two pages.
            middleNFTokenPage = env.le(keylet::nftpage(
                keylet::nftpage_min(alice), middleNFTokenPageIndex));
            if (!BEAST_EXPECT(middleNFTokenPage))
                return;
            BEAST_EXPECT(!middleNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(middleNFTokenPage->isFieldPresent(sfNextPageMin));

            lastNFTokenPage = env.le(keylet::nftpage_max(alice));
            if (!BEAST_EXPECT(lastNFTokenPage))
                return;
            BEAST_EXPECT(lastNFTokenPage->isFieldPresent(sfPreviousPageMin));
            BEAST_EXPECT(!lastNFTokenPage->isFieldPresent(sfNextPageMin));

            // Burn all the tokens in the last page.
            std::reverse(nfts.begin(), nfts.end());
            for (int i = 0; i < 32; ++i)
            {
                env(token::burn(alice, {nfts.back()}));
                nfts.pop_back();
                env.close();
            }

            if (features[fixNFTokenPageLinks])
            {
                // Removing the last token from the last page deletes the
                // _previous_ page because we need to preserve that last
                // page an an anchor.  The contents of the next-to-last page
                // are moved into the last page.
                lastNFTokenPage = env.le(keylet::nftpage_max(alice));
                BEAST_EXPECT(lastNFTokenPage);
                BEAST_EXPECT(
                    !lastNFTokenPage->isFieldPresent(sfPreviousPageMin));
                BEAST_EXPECT(!lastNFTokenPage->isFieldPresent(sfNextPageMin));
                BEAST_EXPECT(
                    lastNFTokenPage->getFieldArray(sfNFTokens).size() == 32);

                // The "middle" page should be gone.
                middleNFTokenPage = env.le(keylet::nftpage(
                    keylet::nftpage_min(alice), middleNFTokenPageIndex));
                BEAST_EXPECT(!middleNFTokenPage);

                // The "first" page should still be gone.
                firstNFTokenPage = env.le(keylet::nftpage(
                    keylet::nftpage_min(alice), firstNFTokenPageIndex));
                BEAST_EXPECT(!firstNFTokenPage);
            }
            else
            {
                // Removing the last token from the last page deletes the last
                // page.  This is a bug.  The contents of the next-to-last page
                // should have been moved into the last page.
                lastNFTokenPage = env.le(keylet::nftpage_max(alice));
                BEAST_EXPECT(!lastNFTokenPage);

                // The "middle" page is still present, but has lost the
                // NextPageMin field.
                middleNFTokenPage = env.le(keylet::nftpage(
                    keylet::nftpage_min(alice), middleNFTokenPageIndex));
                if (!BEAST_EXPECT(middleNFTokenPage))
                    return;
                BEAST_EXPECT(
                    !middleNFTokenPage->isFieldPresent(sfPreviousPageMin));
                BEAST_EXPECT(!middleNFTokenPage->isFieldPresent(sfNextPageMin));
            }

            // Delete the rest of the NFTokens.
            while (!nfts.empty())
            {
                env(token::burn(alice, {nfts.back()}));
                nfts.pop_back();
                env.close();
            }
            BEAST_EXPECT(nftCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
        checkNoTokenPages();

        if (features[fixNFTokenPageLinks])
        {
            // Exercise the invariant that the final NFTokenPage of a directory
            // may not be removed if there are NFTokens in other pages of the
            // directory.
            //
            // We're going to fire an Invariant failure that is difficult to
            // cause.  We do it here because the tools are here.
            //
            // See Invariants_test.cpp for examples of other invariant tests
            // that this one is modeled after.

            // Generate three closely packed NFTokenPages.
            std::vector<uint256> nfts = genPackedTokens();
            BEAST_EXPECT(nftCount(env, alice) == 96);
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            // Burn almost all the tokens in the very last page.
            for (int i = 0; i < 31; ++i)
            {
                env(token::burn(alice, {nfts.back()}));
                nfts.pop_back();
                env.close();
            }
            {
                // Create an ApplyContext we can use to run the invariant
                // checks.  These variables must outlive the ApplyContext.
                OpenView ov{*env.current()};
                STTx tx{ttACCOUNT_SET, [](STObject&) {}};
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

                // Verify that the last page is present and contains one NFT.
                auto lastNFTokenPage =
                    ac.view().peek(keylet::nftpage_max(alice));
                if (!BEAST_EXPECT(lastNFTokenPage))
                    return;
                BEAST_EXPECT(
                    lastNFTokenPage->getFieldArray(sfNFTokens).size() == 1);

                // Erase that last page.
                ac.view().erase(lastNFTokenPage);

                // Exercise the invariant.
                TER terActual = tesSUCCESS;
                for (TER const& terExpect :
                     {TER(tecINVARIANT_FAILED), TER(tefINVARIANT_FAILED)})
                {
                    terActual = ac.checkInvariants(terActual, XRPAmount{});
                    BEAST_EXPECT(terExpect == terActual);
                    BEAST_EXPECT(
                        sink.messages().str().starts_with("Invariant failed:"));
                    // uncomment to log the invariant failure message
                    // log << "   --> " << sink.messages().str() << std::endl;
                    BEAST_EXPECT(
                        sink.messages().str().find(
                            "Last NFT page deleted with non-empty directory") !=
                        std::string::npos);
                }
            }
            {
                // Create an ApplyContext we can use to run the invariant
                // checks.  These variables must outlive the ApplyContext.
                OpenView ov{*env.current()};
                STTx tx{ttACCOUNT_SET, [](STObject&) {}};
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

                // Verify that the middle  page is present.
                auto lastNFTokenPage =
                    ac.view().peek(keylet::nftpage_max(alice));
                auto middleNFTokenPage = ac.view().peek(keylet::nftpage(
                    keylet::nftpage_min(alice),
                    lastNFTokenPage->getFieldH256(sfPreviousPageMin)));
                BEAST_EXPECT(middleNFTokenPage);

                // Remove the NextMinPage link from the middle page to fire
                // the invariant.
                middleNFTokenPage->makeFieldAbsent(sfNextPageMin);
                ac.view().update(middleNFTokenPage);

                // Exercise the invariant.
                TER terActual = tesSUCCESS;
                for (TER const& terExpect :
                     {TER(tecINVARIANT_FAILED), TER(tefINVARIANT_FAILED)})
                {
                    terActual = ac.checkInvariants(terActual, XRPAmount{});
                    BEAST_EXPECT(terExpect == terActual);
                    BEAST_EXPECT(
                        sink.messages().str().starts_with("Invariant failed:"));
                    // uncomment to log the invariant failure message
                    // log << "   --> " << sink.messages().str() << std::endl;
                    BEAST_EXPECT(
                        sink.messages().str().find("Lost NextMinPage link") !=
                        std::string::npos);
                }
            }
        }
    }

    void
    testBurnTooManyOffers(FeatureBitset features)
    {
        // Look at the case where too many offers prevents burning a token.
        testcase("Burn too many offers");

        using namespace test::jtx;

        // Test what happens if a NFT is unburnable when there are
        // more than 500 offers, before fixNonFungibleTokensV1_2 goes live
        if (!features[fixNonFungibleTokensV1_2])
        {
            Env env{*this, features};

            Account const alice("alice");
            Account const becky("becky");
            env.fund(XRP(1000), alice, becky);
            env.close();

            // We structure the test to try and maximize the metadata produced.
            // This verifies that we don't create too much metadata during a
            // maximal burn operation.
            //
            // 1. alice mints an nft with a full-sized URI.
            // 2. We create 500 new accounts, each of which creates an offer
            //    for alice's nft.
            // 3. becky creates one more offer for alice's NFT
            // 4. Attempt to burn the nft which fails because there are too
            //    many offers.
            // 5. Cancel becky's offer and the nft should become burnable.
            uint256 const nftokenID =
                token::getNextID(env, alice, 0, tfTransferable);
            env(token::mint(alice, 0),
                token::uri(std::string(maxTokenURILength, 'u')),
                txflags(tfTransferable));
            env.close();

            std::vector<uint256> offerIndexes;
            offerIndexes.reserve(maxTokenOfferCancelCount);
            for (std::uint32_t i = 0; i < maxTokenOfferCancelCount; ++i)
            {
                Account const acct(std::string("acct") + std::to_string(i));
                env.fund(XRP(1000), acct);
                env.close();

                offerIndexes.push_back(
                    keylet::nftoffer(acct, env.seq(acct)).key);
                env(token::createOffer(acct, nftokenID, drops(1)),
                    token::owner(alice));
                env.close();
            }

            // Verify all offers are present in the ledger.
            for (uint256 const& offerIndex : offerIndexes)
            {
                BEAST_EXPECT(env.le(keylet::nftoffer(offerIndex)));
            }

            // Create one too many offers.
            uint256 const beckyOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftokenID, drops(1)),
                token::owner(alice));

            // Attempt to burn the nft which should fail.
            env(token::burn(alice, nftokenID), ter(tefTOO_BIG));

            // Close enough ledgers that the burn transaction is no longer
            // retried.
            for (int i = 0; i < 10; ++i)
                env.close();

            // Cancel becky's offer, but alice adds a sell offer.  The token
            // should still not be burnable.
            env(token::cancelOffer(becky, {beckyOfferIndex}));
            env.close();

            uint256 const aliceOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftokenID, drops(1)),
                txflags(tfSellNFToken));
            env.close();

            env(token::burn(alice, nftokenID), ter(tefTOO_BIG));
            env.close();

            // Cancel alice's sell offer.  Now the token should be burnable.
            env(token::cancelOffer(alice, {aliceOfferIndex}));
            env.close();

            env(token::burn(alice, nftokenID));
            env.close();

            // Burning the token should remove all the offers from the ledger.
            for (uint256 const& offerIndex : offerIndexes)
            {
                BEAST_EXPECT(!env.le(keylet::nftoffer(offerIndex)));
            }

            // Both alice and becky should have ownerCounts of zero.
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 0);
        }

        // Test that up to 499 buy/sell offers will be removed when NFT is
        // burned after fixNonFungibleTokensV1_2 is enabled. This is to test
        // that we can successfully remove all offers if the number of offers is
        // less than 500.
        if (features[fixNonFungibleTokensV1_2])
        {
            Env env{*this, features};

            Account const alice("alice");
            Account const becky("becky");
            env.fund(XRP(100000), alice, becky);
            env.close();

            // alice creates 498 sell offers and becky creates 1 buy offers.
            // When the token is burned, 498 sell offers and 1 buy offer are
            // removed. In total, 499 offers are removed
            std::vector<uint256> offerIndexes;
            auto const nftokenID = createNftAndOffers(
                env, alice, offerIndexes, maxDeletableTokenOfferEntries - 2);

            // Verify all sell offers are present in the ledger.
            for (uint256 const& offerIndex : offerIndexes)
            {
                BEAST_EXPECT(env.le(keylet::nftoffer(offerIndex)));
            }

            // Becky creates a buy offer
            uint256 const beckyOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftokenID, drops(1)),
                token::owner(alice));
            env.close();

            // Burn the token
            env(token::burn(alice, nftokenID));
            env.close();

            // Burning the token should remove all 498 sell offers
            // that alice created
            for (uint256 const& offerIndex : offerIndexes)
            {
                BEAST_EXPECT(!env.le(keylet::nftoffer(offerIndex)));
            }

            // Burning the token should also remove the one buy offer
            // that becky created
            BEAST_EXPECT(!env.le(keylet::nftoffer(beckyOfferIndex)));

            // alice and becky should have ownerCounts of zero
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 0);
        }

        // Test that up to 500 buy offers are removed when NFT is burned
        // after fixNonFungibleTokensV1_2 is enabled
        if (features[fixNonFungibleTokensV1_2])
        {
            Env env{*this, features};

            Account const alice("alice");
            Account const becky("becky");
            env.fund(XRP(100000), alice, becky);
            env.close();

            // alice creates 501 sell offers for the token
            // After we burn the token, 500 of the sell offers should be
            // removed, and one is left over
            std::vector<uint256> offerIndexes;
            auto const nftokenID = createNftAndOffers(
                env, alice, offerIndexes, maxDeletableTokenOfferEntries + 1);

            // Verify all sell offers are present in the ledger.
            for (uint256 const& offerIndex : offerIndexes)
            {
                BEAST_EXPECT(env.le(keylet::nftoffer(offerIndex)));
            }

            // Burn the token
            env(token::burn(alice, nftokenID));
            env.close();

            uint32_t offerDeletedCount = 0;
            // Count the number of sell offers that have been deleted
            for (uint256 const& offerIndex : offerIndexes)
            {
                if (!env.le(keylet::nftoffer(offerIndex)))
                    offerDeletedCount++;
            }

            BEAST_EXPECT(offerIndexes.size() == maxTokenOfferCancelCount + 1);

            // 500 sell offers should be removed
            BEAST_EXPECT(offerDeletedCount == maxTokenOfferCancelCount);

            // alice should have ownerCounts of one for the orphaned sell offer
            BEAST_EXPECT(ownerCount(env, alice) == 1);
        }

        // Test that up to 500 buy/sell offers are removed when NFT is burned
        // after fixNonFungibleTokensV1_2 is enabled
        if (features[fixNonFungibleTokensV1_2])
        {
            Env env{*this, features};

            Account const alice("alice");
            Account const becky("becky");
            env.fund(XRP(100000), alice, becky);
            env.close();

            // alice creates 499 sell offers and becky creates 2 buy offers.
            // When the token is burned, 499 sell offers and 1 buy offer
            // are removed.
            // In total, 500 offers are removed
            std::vector<uint256> offerIndexes;
            auto const nftokenID = createNftAndOffers(
                env, alice, offerIndexes, maxDeletableTokenOfferEntries - 1);

            // Verify all sell offers are present in the ledger.
            for (uint256 const& offerIndex : offerIndexes)
            {
                BEAST_EXPECT(env.le(keylet::nftoffer(offerIndex)));
            }

            // becky creates 2 buy offers
            env(token::createOffer(becky, nftokenID, drops(1)),
                token::owner(alice));
            env.close();
            env(token::createOffer(becky, nftokenID, drops(1)),
                token::owner(alice));
            env.close();

            // Burn the token
            env(token::burn(alice, nftokenID));
            env.close();

            // Burning the token should remove all 499 sell offers from the
            // ledger.
            for (uint256 const& offerIndex : offerIndexes)
            {
                BEAST_EXPECT(!env.le(keylet::nftoffer(offerIndex)));
            }

            // alice should have ownerCount of zero because all her
            // sell offers have been deleted
            BEAST_EXPECT(ownerCount(env, alice) == 0);

            // becky has ownerCount of one due to an orphaned buy offer
            BEAST_EXPECT(ownerCount(env, becky) == 1);
        }
    }

    void
    exerciseBrokenLinks(FeatureBitset features)
    {
        // Amendment fixNFTokenPageLinks prevents the breakage we want
        // to observe.
        if (features[fixNFTokenPageLinks])
            return;

        // a couple of directory merging scenarios that can only be tested by
        // inserting and deleting in an ordered fashion.  We do that testing
        // now.
        testcase("Exercise broken links");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const minter{"minter"};

        Env env{*this, features};
        env.fund(XRP(1000), alice, minter);

        // A lambda that generates 96 nfts packed into three pages of 32 each.
        // Returns a sorted vector of the NFTokenIDs packed into the pages.
        auto genPackedTokens = [this, &env, &alice, &minter]() {
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
            auto internalTaxon = [&env](
                                     Account const& acct,
                                     std::uint32_t taxon) -> std::uint32_t {
                std::uint32_t tokenSeq =
                    env.le(acct)->at(~sfMintedNFTokens).value_or(0);

                // If fixNFTokenRemint amendment is on, we must
                // add FirstNFTokenSequence.
                if (env.current()->rules().enabled(fixNFTokenRemint))
                    tokenSeq += env.le(acct)
                                    ->at(~sfFirstNFTokenSequence)
                                    .value_or(env.seq(acct));

                return toUInt32(
                    nft::cipheredTaxon(tokenSeq, nft::toTaxon(taxon)));
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
                uint32_t const extTaxon = internalTaxon(minter, intTaxon);
                nfts.push_back(
                    token::getNextID(env, minter, extTaxon, tfTransferable));
                env(token::mint(minter, extTaxon), txflags(tfTransferable));
                env.close();

                // Minter creates an offer for the NFToken.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nfts.back(), XRP(0)),
                    txflags(tfSellNFToken));
                env.close();

                // alice accepts the offer.
                env(token::acceptSellOffer(alice, minterOfferIndex));
                env.close();
            }

            // Sort the NFTs so they are listed in storage order, not
            // creation order.
            std::sort(nfts.begin(), nfts.end());

            // Verify that the ledger does indeed contain exactly three pages
            // of NFTs with 32 entries in each page.
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "current";
            jvParams[jss::binary] = false;
            {
                Json::Value jrr = env.rpc(
                    "json",
                    "ledger_data",
                    boost::lexical_cast<std::string>(jvParams));

                Json::Value& state = jrr[jss::result][jss::state];

                int pageCount = 0;
                for (Json::UInt i = 0; i < state.size(); ++i)
                {
                    if (state[i].isMember(sfNFTokens.jsonName) &&
                        state[i][sfNFTokens.jsonName].isArray())
                    {
                        BEAST_EXPECT(
                            state[i][sfNFTokens.jsonName].size() == 32);
                        ++pageCount;
                    }
                }
                // If this check fails then the internal NFT directory logic
                // has changed.
                BEAST_EXPECT(pageCount == 3);
            }
            return nfts;
        };

        // Generate three packed pages.
        std::vector<uint256> nfts = genPackedTokens();
        BEAST_EXPECT(nftCount(env, alice) == 96);
        BEAST_EXPECT(ownerCount(env, alice) == 3);

        // Verify that that all three pages are present and remember the
        // indexes.
        auto lastNFTokenPage = env.le(keylet::nftpage_max(alice));
        if (!BEAST_EXPECT(lastNFTokenPage))
            return;

        uint256 const middleNFTokenPageIndex =
            lastNFTokenPage->at(sfPreviousPageMin);
        auto middleNFTokenPage = env.le(keylet::nftpage(
            keylet::nftpage_min(alice), middleNFTokenPageIndex));
        if (!BEAST_EXPECT(middleNFTokenPage))
            return;

        uint256 const firstNFTokenPageIndex =
            middleNFTokenPage->at(sfPreviousPageMin);
        auto firstNFTokenPage = env.le(
            keylet::nftpage(keylet::nftpage_min(alice), firstNFTokenPageIndex));
        if (!BEAST_EXPECT(firstNFTokenPage))
            return;

        // Sell all the tokens in the very last page back to minter.
        std::vector<uint256> last32NFTs;
        for (int i = 0; i < 32; ++i)
        {
            last32NFTs.push_back(nfts.back());
            nfts.pop_back();

            // alice creates an offer for the NFToken.
            uint256 const aliceOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, last32NFTs.back(), XRP(0)),
                txflags(tfSellNFToken));
            env.close();

            // minter accepts the offer.
            env(token::acceptSellOffer(minter, aliceOfferIndex));
            env.close();
        }

        // Removing the last token from the last page deletes alice's last
        // page.  This is a bug.  The contents of the next-to-last page
        // should have been moved into the last page.
        lastNFTokenPage = env.le(keylet::nftpage_max(alice));
        BEAST_EXPECT(!lastNFTokenPage);
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        // The "middle" page is still present, but has lost the
        // NextPageMin field.
        middleNFTokenPage = env.le(keylet::nftpage(
            keylet::nftpage_min(alice), middleNFTokenPageIndex));
        if (!BEAST_EXPECT(middleNFTokenPage))
            return;
        BEAST_EXPECT(middleNFTokenPage->isFieldPresent(sfPreviousPageMin));
        BEAST_EXPECT(!middleNFTokenPage->isFieldPresent(sfNextPageMin));

        // Attempt to delete alice's account, but fail because she owns NFTs.
        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(alice, minter),
            fee(acctDelFee),
            ter(tecHAS_OBLIGATIONS));
        env.close();

        // minter sells the last 32 NFTs back to alice.
        for (uint256 nftID : last32NFTs)
        {
            // minter creates an offer for the NFToken.
            uint256 const minterOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, XRP(0)),
                txflags(tfSellNFToken));
            env.close();

            // alice accepts the offer.
            env(token::acceptSellOffer(alice, minterOfferIndex));
            env.close();
        }
        BEAST_EXPECT(ownerCount(env, alice) == 3);  // Three NFTokenPages.

        // alice has an NFToken directory with a broken link in the middle.
        {
            // Try the account_objects RPC command.  Alice's account only shows
            // two NFT pages even though she owns more.
            Json::Value acctObjs = [&env, &alice]() {
                Json::Value params;
                params[jss::account] = alice.human();
                return env.rpc("json", "account_objects", to_string(params));
            }();
            BEAST_EXPECT(!acctObjs.isMember(jss::marker));
            BEAST_EXPECT(
                acctObjs[jss::result][jss::account_objects].size() == 2);
        }
        {
            // Try the account_nfts RPC command.  It only returns 64 NFTs
            // although alice owns 96.
            Json::Value aliceNFTs = [&env, &alice]() {
                Json::Value params;
                params[jss::account] = alice.human();
                params[jss::type] = "state";
                return env.rpc("json", "account_nfts", to_string(params));
            }();
            BEAST_EXPECT(!aliceNFTs.isMember(jss::marker));
            BEAST_EXPECT(
                aliceNFTs[jss::result][jss::account_nfts].size() == 64);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testBurnRandom(features);
        testBurnSequential(features);
        testBurnTooManyOffers(features);
        exerciseBrokenLinks(features);
    }

protected:
    void
    run(std::uint32_t instance, bool last = false)
    {
        using namespace test::jtx;
        static FeatureBitset const all{supported_amendments()};
        static FeatureBitset const fixNFTV1_2{fixNonFungibleTokensV1_2};
        static FeatureBitset const fixNFTDir{fixNFTokenDirV1};
        static FeatureBitset const fixNFTRemint{fixNFTokenRemint};
        static FeatureBitset const fixNFTPageLinks{fixNFTokenPageLinks};

        static std::array<FeatureBitset, 5> const feats{
            all - fixNFTV1_2 - fixNFTDir - fixNFTRemint - fixNFTPageLinks,
            all - fixNFTV1_2 - fixNFTRemint - fixNFTPageLinks,
            all - fixNFTRemint - fixNFTPageLinks,
            all - fixNFTPageLinks,
            all,
        };

        if (BEAST_EXPECT(instance < feats.size()))
        {
            testWithFeats(feats[instance]);
        }
        BEAST_EXPECT(!last || instance == feats.size() - 1);
    }

public:
    void
    run() override
    {
        run(0);
    }
};

class NFTokenBurnWOfixFungTokens_test : public NFTokenBurnBaseUtil_test
{
public:
    void
    run() override
    {
        NFTokenBurnBaseUtil_test::run(1);
    }
};

class NFTokenBurnWOFixTokenRemint_test : public NFTokenBurnBaseUtil_test
{
public:
    void
    run() override
    {
        NFTokenBurnBaseUtil_test::run(2);
    }
};

class NFTokenBurnWOFixNFTPageLinks_test : public NFTokenBurnBaseUtil_test
{
public:
    void
    run() override
    {
        NFTokenBurnBaseUtil_test::run(3);
    }
};

class NFTokenBurnAllFeatures_test : public NFTokenBurnBaseUtil_test
{
public:
    void
    run() override
    {
        NFTokenBurnBaseUtil_test::run(4, true);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(NFTokenBurnBaseUtil, tx, ripple, 3);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenBurnWOfixFungTokens, tx, ripple, 3);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenBurnWOFixTokenRemint, tx, ripple, 3);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenBurnWOFixNFTPageLinks, tx, ripple, 3);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenBurnAllFeatures, tx, ripple, 3);

}  // namespace ripple
