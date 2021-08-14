//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/nftPageMask.h>
#include <test/jtx.h>

#include <initializer_list>

namespace ripple {

class NFTokenDir_test : public beast::unit_test::suite
{
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
    testLopsidedSplits(FeatureBitset features)
    {
        // All NFT IDs with the same low 96 bits must stay on the same NFT page.
        testcase("Lopsided splits");

        using namespace test::jtx;

        // When a single NFT page exceeds 32 entries, the code is inclined
        // to split that page into two equal pieces.  That's fine, but
        // the code also needs to keep NFTs with identical low 96-bits on
        // the same page.
        //
        // Here we synthesize cases where there are several NFTs with
        // identical 96-low-bits in the middle of a page.  When that page
        // is split because it overflows, we need to see that the NFTs
        // with identical 96-low-bits are all kept on the same page.

        // Lambda that exercises the lopsided splits.
        auto exerciseLopsided =
            [this,
             &features](std::initializer_list<std::string_view const> seeds) {
                Env env{*this, features};

                // Eventually all of the NFTokens will be owned by buyer.
                Account const buyer{"buyer"};
                env.fund(XRP(10000), buyer);
                env.close();

                // Create accounts for all of the seeds and fund those accounts.
                std::vector<Account> accounts;
                accounts.reserve(seeds.size());
                for (std::string_view const& seed : seeds)
                {
                    Account const& account = accounts.emplace_back(
                        Account::base58Seed, std::string(seed));
                    env.fund(XRP(10000), account);
                    env.close();
                }

                // All of the accounts create one NFT and and offer that NFT to
                // buyer.
                std::vector<uint256> nftIDs;
                std::vector<uint256> offers;
                offers.reserve(accounts.size());
                for (Account const& account : accounts)
                {
                    // Mint the NFT.
                    uint256 const& nftID = nftIDs.emplace_back(
                        token::getNextID(env, account, 0, tfTransferable));
                    env(token::mint(account, 0), txflags(tfTransferable));
                    env.close();

                    // Create an offer to give the NFT to buyer for free.
                    offers.emplace_back(
                        keylet::nftoffer(account, env.seq(account)).key);
                    env(token::createOffer(account, nftID, XRP(0)),
                        token::destination(buyer),
                        txflags((tfSellNFToken)));
                }
                env.close();

                // buyer accepts all of the offers.
                for (uint256 const& offer : offers)
                {
                    env(token::acceptSellOffer(buyer, offer));
                    env.close();
                }

                // This can be a good time to look at the NFT pages.
                // printNFTPages(env, noisy);

                // Verify that all NFTs are owned by buyer and findable in the
                // ledger by having buyer create sell offers for all of their
                // NFTs. Attempting to sell an offer that the ledger can't find
                // generates a non-tesSUCCESS error code.
                for (uint256 const& nftID : nftIDs)
                {
                    uint256 const offerID =
                        keylet::nftoffer(buyer, env.seq(buyer)).key;
                    env(token::createOffer(buyer, nftID, XRP(100)),
                        txflags(tfSellNFToken));
                    env.close();

                    env(token::cancelOffer(buyer, {offerID}));
                }

                // Verify that all the NFTs are owned by buyer.
                Json::Value buyerNFTs = [&env, &buyer]() {
                    Json::Value params;
                    params[jss::account] = buyer.human();
                    params[jss::type] = "state";
                    return env.rpc("json", "account_nfts", to_string(params));
                }();

                BEAST_EXPECT(
                    buyerNFTs[jss::result][jss::account_nfts].size() ==
                    nftIDs.size());
                for (Json::Value const& ownedNFT :
                     buyerNFTs[jss::result][jss::account_nfts])
                {
                    uint256 ownedID;
                    BEAST_EXPECT(ownedID.parseHex(
                        ownedNFT[sfNFTokenID.jsonName].asString()));
                    auto const foundIter =
                        std::find(nftIDs.begin(), nftIDs.end(), ownedID);

                    // Assuming we find the NFT, erase it so we know it's been
                    // found and can't be found again.
                    if (BEAST_EXPECT(foundIter != nftIDs.end()))
                        nftIDs.erase(foundIter);
                }

                // All NFTs should now be accounted for, so nftIDs should be
                // empty.
                BEAST_EXPECT(nftIDs.empty());
            };

        // These seeds cause a lopsided split where the new NFT is added
        // to the upper page.
        static std::initializer_list<std::string_view const> const
            splitAndAddToHi{
                "sp6JS7f14BuwFY8Mw5p3b8jjQBBTK",  //  0. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6F7X3EiGKazu",  //  1. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6FxjntJJfKXq",  //  2. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6eSF1ydEozJg",  //  3. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6koPB91um2ej",  //  4. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6m6D64iwquSe",  //  5. 0x1d2932ea

                "sp6JS7f14BuwFY8Mw5rC43sN4adC2",  //  6. 0x208dbc24
                "sp6JS7f14BuwFY8Mw65L9DDQqgebz",  //  7. 0x208dbc24
                "sp6JS7f14BuwFY8Mw65nKvU8pPQNn",  //  8. 0x208dbc24
                "sp6JS7f14BuwFY8Mw6bxZLyTrdipw",  //  9. 0x208dbc24
                "sp6JS7f14BuwFY8Mw6d5abucntSoX",  // 10. 0x208dbc24
                "sp6JS7f14BuwFY8Mw6qXK5awrRRP8",  // 11. 0x208dbc24

                // These eight need to be kept together by the implementation.
                "sp6JS7f14BuwFY8Mw66EBtMxoMcCa",  // 12. 0x309b67ed
                "sp6JS7f14BuwFY8Mw66dGfE9jVfGv",  // 13. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6APdZa7PH566",  // 14. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6C3QX5CZyET5",  // 15. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6CSysFf8GvaR",  // 16. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6c7QSDmoAeRV",  // 17. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6mvonveaZhW7",  // 18. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6vtHHG7dYcXi",  // 19. 0x309b67ed

                "sp6JS7f14BuwFY8Mw66yppUNxESaw",  // 20. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6ATYQvobXiDT",  // 21. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6bis8D1Wa9Uy",  // 22. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6cTiGCWA8Wfa",  // 23. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6eAy2fpXmyYf",  // 24. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6icn58TRs8YG",  // 25. 0x40d4b96f

                "sp6JS7f14BuwFY8Mw68tj2eQEWoJt",  // 26. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6AjnAinNnMHT",  // 27. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6CKDUwB4LrhL",  // 28. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6d2yPszEFA6J",  // 29. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6jcBQBH3PfnB",  // 30. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6qxx19KSnN1w",  // 31. 0x503b6ba9

                // Adding this NFT splits the page.  It is added to the upper
                // page.
                "sp6JS7f14BuwFY8Mw6ut1hFrqWoY5",  // 32. 0x503b6ba9
            };

        // These seeds cause a lopsided split where the new NFT is added
        // to the lower page.
        static std::initializer_list<std::string_view const> const
            splitAndAddToLo{
                "sp6JS7f14BuwFY8Mw5p3b8jjQBBTK",  //  0. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6F7X3EiGKazu",  //  1. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6FxjntJJfKXq",  //  2. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6eSF1ydEozJg",  //  3. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6koPB91um2ej",  //  4. 0x1d2932ea
                "sp6JS7f14BuwFY8Mw6m6D64iwquSe",  //  5. 0x1d2932ea

                "sp6JS7f14BuwFY8Mw5rC43sN4adC2",  //  6. 0x208dbc24
                "sp6JS7f14BuwFY8Mw65L9DDQqgebz",  //  7. 0x208dbc24
                "sp6JS7f14BuwFY8Mw65nKvU8pPQNn",  //  8. 0x208dbc24
                "sp6JS7f14BuwFY8Mw6bxZLyTrdipw",  //  9. 0x208dbc24
                "sp6JS7f14BuwFY8Mw6d5abucntSoX",  // 10. 0x208dbc24
                "sp6JS7f14BuwFY8Mw6qXK5awrRRP8",  // 11. 0x208dbc24

                // These eight need to be kept together by the implementation.
                "sp6JS7f14BuwFY8Mw66EBtMxoMcCa",  // 12. 0x309b67ed
                "sp6JS7f14BuwFY8Mw66dGfE9jVfGv",  // 13. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6APdZa7PH566",  // 14. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6C3QX5CZyET5",  // 15. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6CSysFf8GvaR",  // 16. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6c7QSDmoAeRV",  // 17. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6mvonveaZhW7",  // 18. 0x309b67ed
                "sp6JS7f14BuwFY8Mw6vtHHG7dYcXi",  // 19. 0x309b67ed

                "sp6JS7f14BuwFY8Mw66yppUNxESaw",  // 20. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6ATYQvobXiDT",  // 21. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6bis8D1Wa9Uy",  // 22. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6cTiGCWA8Wfa",  // 23. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6eAy2fpXmyYf",  // 24. 0x40d4b96f
                "sp6JS7f14BuwFY8Mw6icn58TRs8YG",  // 25. 0x40d4b96f

                "sp6JS7f14BuwFY8Mw68tj2eQEWoJt",  // 26. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6AjnAinNnMHT",  // 27. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6CKDUwB4LrhL",  // 28. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6d2yPszEFA6J",  // 29. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6jcBQBH3PfnB",  // 30. 0x503b6ba9
                "sp6JS7f14BuwFY8Mw6qxx19KSnN1w",  // 31. 0x503b6ba9

                // Adding this NFT splits the page.  It is added to the lower
                // page.
                "sp6JS7f14BuwFY8Mw6xCigaMwC6Dp",  // 32. 0x309b67ed
            };

        // FUTURE TEST
        // These seeds fill the last 17 entries of the initial page with
        // equivalent NFTs.  The split should keep these together.

        // FUTURE TEST
        // These seeds fill the first entries of the initial page with
        // equivalent NFTs.  The split should keep these together.

        // Run the test cases.
        exerciseLopsided(splitAndAddToHi);
        exerciseLopsided(splitAndAddToLo);
    }

    void
    testTooManyEquivalent(FeatureBitset features)
    {
        // Exercise the case where 33 NFTs with identical sort
        // characteristics are owned by the same account.
        testcase("NFToken too many same");

        using namespace test::jtx;

        Env env{*this, features};

        // Eventually all of the NFTokens will be owned by buyer.
        Account const buyer{"buyer"};
        env.fund(XRP(10000), buyer);
        env.close();

        // Here are 33 seeds that produce identical low 32-bits in their
        // corresponding AccountIDs.
        //
        // NOTE: We've not yet identified 33 AccountIDs that meet the
        // requirements.  At the moment 12 is the best we can do.  We'll fill
        // in the full count when they are available.
        static std::initializer_list<std::string_view const> const seeds{
            "sp6JS7f14BuwFY8Mw5G5vCrbxB3TZ",
            "sp6JS7f14BuwFY8Mw5H6qyXhorcip",
            "sp6JS7f14BuwFY8Mw5suWxsBQRqLx",
            "sp6JS7f14BuwFY8Mw66gtwamvGgSg",
            "sp6JS7f14BuwFY8Mw66iNV4PPcmyt",
            "sp6JS7f14BuwFY8Mw68Qz2P58ybfE",
            "sp6JS7f14BuwFY8Mw6AYtLXKzi2Bo",
            "sp6JS7f14BuwFY8Mw6boCES4j62P2",
            "sp6JS7f14BuwFY8Mw6kv7QDDv7wjw",
            "sp6JS7f14BuwFY8Mw6mHXMvpBjjwg",
            "sp6JS7f14BuwFY8Mw6qfGbznyYvVp",
            "sp6JS7f14BuwFY8Mw6zg6qHKDfSoU",
        };

        // Create accounts for all of the seeds and fund those accounts.
        std::vector<Account> accounts;
        accounts.reserve(seeds.size());
        for (std::string_view const& seed : seeds)
        {
            Account const& account =
                accounts.emplace_back(Account::base58Seed, std::string(seed));
            env.fund(XRP(10000), account);
            env.close();
        }

        // All of the accounts create one NFT and and offer that NFT to buyer.
        std::vector<uint256> nftIDs;
        std::vector<uint256> offers;
        offers.reserve(accounts.size());
        for (Account const& account : accounts)
        {
            // Mint the NFT.
            uint256 const& nftID = nftIDs.emplace_back(
                token::getNextID(env, account, 0, tfTransferable));
            env(token::mint(account, 0), txflags(tfTransferable));
            env.close();

            // Create an offer to give the NFT to buyer for free.
            offers.emplace_back(
                keylet::nftoffer(account, env.seq(account)).key);
            env(token::createOffer(account, nftID, XRP(0)),
                token::destination(buyer),
                txflags((tfSellNFToken)));
        }
        env.close();

        // Verify that the low 96 bits of all generated NFTs is identical.
        uint256 const expectLowBits = nftIDs.front() & nft::pageMask;
        for (uint256 const& nftID : nftIDs)
        {
            BEAST_EXPECT(expectLowBits == (nftID & nft::pageMask));
        }

        // buyer accepts all of the offers.
        for (uint256 const& offer : offers)
        {
            env(token::acceptSellOffer(buyer, offer));
            env.close();
        }

        // Verify that all NFTs are owned by buyer and findable in the
        // ledger by having buyer create sell offers for all of their NFTs.
        // Attempting to sell an offer that the ledger can't find generates
        // a non-tesSUCCESS error code.
        for (uint256 const& nftID : nftIDs)
        {
            uint256 const offerID = keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftID, XRP(100)),
                txflags(tfSellNFToken));
            env.close();

            env(token::cancelOffer(buyer, {offerID}));
        }

        // Verify that all the NFTs are owned by buyer.
        Json::Value buyerNFTs = [&env, &buyer]() {
            Json::Value params;
            params[jss::account] = buyer.human();
            params[jss::type] = "state";
            return env.rpc("json", "account_nfts", to_string(params));
        }();

        BEAST_EXPECT(
            buyerNFTs[jss::result][jss::account_nfts].size() == nftIDs.size());
        for (Json::Value const& ownedNFT :
             buyerNFTs[jss::result][jss::account_nfts])
        {
            uint256 ownedID;
            BEAST_EXPECT(
                ownedID.parseHex(ownedNFT[sfNFTokenID.jsonName].asString()));
            auto const foundIter =
                std::find(nftIDs.begin(), nftIDs.end(), ownedID);

            // Assuming we find the NFT, erase it so we know it's been found
            // and can't be found again.
            if (BEAST_EXPECT(foundIter != nftIDs.end()))
                nftIDs.erase(foundIter);
        }

        // All NFTs should now be accounted for, so nftIDs should be empty.
        BEAST_EXPECT(nftIDs.empty());
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testLopsidedSplits(features);
        testTooManyEquivalent(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(NFTokenDir, tx, ripple, 1);

}  // namespace ripple
