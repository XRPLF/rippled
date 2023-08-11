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

#include <ripple/app/tx/impl/details/NFTokenUtils.h>
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
    testConsecutiveNFTs(FeatureBitset features)
    {
        // It should be possible to store many consecutive NFTs.
        testcase("Sequential NFTs");

        using namespace test::jtx;
        Env env{*this, features};

        // A single minter tends not to mint numerically sequential NFTokens
        // because the taxon cipher mixes things up.  We can override the
        // cipher, however, and mint many sequential NFTokens with no gaps
        // between them.
        //
        // Here we'll simply mint 100 sequential NFTs.  Then we'll create
        // offers for them to verify that the ledger can find them.

        Account const issuer{"issuer"};
        Account const buyer{"buyer"};
        env.fund(XRP(10000), buyer, issuer);
        env.close();

        // Mint 100 sequential NFTs.  Tweak the taxon so zero is always stored.
        // That's what makes them sequential.
        constexpr std::size_t nftCount = 100;
        std::vector<uint256> nftIDs;
        nftIDs.reserve(nftCount);
        for (int i = 0; i < nftCount; ++i)
        {
            std::uint32_t taxon =
                toUInt32(nft::cipheredTaxon(i, nft::toTaxon(0)));
            nftIDs.emplace_back(
                token::getNextID(env, issuer, taxon, tfTransferable));
            env(token::mint(issuer, taxon), txflags(tfTransferable));
            env.close();
        }

        // Create an offer for each of the NFTs.  This verifies that the ledger
        // can find all of the minted NFTs.
        std::vector<uint256> offers;
        for (uint256 const& nftID : nftIDs)
        {
            offers.emplace_back(keylet::nftoffer(issuer, env.seq(issuer)).key);
            env(token::createOffer(issuer, nftID, XRP(0)),
                txflags((tfSellNFToken)));
            env.close();
        }

        // Buyer accepts all of the offers in reverse order.
        std::reverse(offers.begin(), offers.end());
        for (uint256 const& offer : offers)
        {
            env(token::acceptSellOffer(buyer, offer));
            env.close();
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

                    // Do not close the ledger inside the loop.  If
                    // fixNFTokenRemint is enabled and accounts are initialized
                    // at different ledgers, they will have different account
                    // sequences.  That would cause the accounts to have
                    // different NFTokenID sequence numbers.
                }
                env.close();

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

        // Run the test cases.
        exerciseLopsided(splitAndAddToHi);
        exerciseLopsided(splitAndAddToLo);
    }

    void
    testFixNFTokenDirV1(FeatureBitset features)
    {
        // Exercise a fix for an off-by-one in the creation of an NFTokenPage
        // index.
        testcase("fixNFTokenDirV1");

        using namespace test::jtx;

        // When a single NFT page exceeds 32 entries, the code is inclined
        // to split that page into two equal pieces.  The new page is lower
        // than the original.  There was an off-by-one in the selection of
        // the index for the new page.  This test recreates the problem.

        // Lambda that exercises the split.
        auto exerciseFixNFTokenDirV1 =
            [this,
             &features](std::initializer_list<std::string_view const> seeds) {
                Env env{
                    *this,
                    envconfig(),
                    features,
                    nullptr,
                    beast::severities::kDisabled};

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

                    // Do not close the ledger inside the loop.  If
                    // fixNFTokenRemint is enabled and accounts are initialized
                    // at different ledgers, they will have different account
                    // sequences.  That would cause the accounts to have
                    // different NFTokenID sequence numbers.
                }
                env.close();

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

                // buyer accepts all of the but the last.  The last offer
                // causes the page to split.
                for (std::size_t i = 0; i < offers.size() - 1; ++i)
                {
                    env(token::acceptSellOffer(buyer, offers[i]));
                    env.close();
                }

                // Here is the last offer.  Without the fix accepting this
                // offer causes tecINVARIANT_FAILED.  With the fix the offer
                // accept succeeds.
                if (!features[fixNFTokenDirV1])
                {
                    env(token::acceptSellOffer(buyer, offers.back()),
                        ter(tecINVARIANT_FAILED));
                    env.close();
                    return;
                }
                env(token::acceptSellOffer(buyer, offers.back()));
                env.close();

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

        // These seeds fill the last 17 entries of the initial page with
        // equivalent NFTs.  The split should keep these together.
        static std::initializer_list<std::string_view const> const seventeenHi{
            // These 16 need to be kept together by the implementation.
            "sp6JS7f14BuwFY8Mw5EYu5z86hKDL",  //  0. 0x399187e9
            "sp6JS7f14BuwFY8Mw5PUAMwc5ygd7",  //  1. 0x399187e9
            "sp6JS7f14BuwFY8Mw5R3xUBcLSeTs",  //  2. 0x399187e9
            "sp6JS7f14BuwFY8Mw5W6oS5sdC3oF",  //  3. 0x399187e9
            "sp6JS7f14BuwFY8Mw5pYc3D9iuLcw",  //  4. 0x399187e9
            "sp6JS7f14BuwFY8Mw5pfGVnhcdp3b",  //  5. 0x399187e9
            "sp6JS7f14BuwFY8Mw6jS6RdEqXqrN",  //  6. 0x399187e9
            "sp6JS7f14BuwFY8Mw6krt6AKbvRXW",  //  7. 0x399187e9
            "sp6JS7f14BuwFY8Mw6mnVBQq7cAN2",  //  8. 0x399187e9
            "sp6JS7f14BuwFY8Mw8ECJxPjmkufQ",  //  9. 0x399187e9
            "sp6JS7f14BuwFY8Mw8asgzcceGWYm",  // 10. 0x399187e9
            "sp6JS7f14BuwFY8MwF6J3FXnPCgL8",  // 11. 0x399187e9
            "sp6JS7f14BuwFY8MwFEud2w5czv5q",  // 12. 0x399187e9
            "sp6JS7f14BuwFY8MwFNxKVqJnx8P5",  // 13. 0x399187e9
            "sp6JS7f14BuwFY8MwFnTCXg3eRidL",  // 14. 0x399187e9
            "sp6JS7f14BuwFY8Mwj47hv1vrDge6",  // 15. 0x399187e9

            // These 17 need to be kept together by the implementation.
            "sp6JS7f14BuwFY8MwjJCwYr9zSfAv",  // 16. 0xabb11898
            "sp6JS7f14BuwFY8MwjYa5yLkgCLuT",  // 17. 0xabb11898
            "sp6JS7f14BuwFY8MwjenxuJ3TH2Bc",  // 18. 0xabb11898
            "sp6JS7f14BuwFY8MwjriN7Ui11NzB",  // 19. 0xabb11898
            "sp6JS7f14BuwFY8Mwk3AuoJNSEo34",  // 20. 0xabb11898
            "sp6JS7f14BuwFY8MwkT36hnRv8hTo",  // 21. 0xabb11898
            "sp6JS7f14BuwFY8MwkTQixEXfi1Cr",  // 22. 0xabb11898
            "sp6JS7f14BuwFY8MwkYJaZM1yTJBF",  // 23. 0xabb11898
            "sp6JS7f14BuwFY8Mwkc4k1uo85qp2",  // 24. 0xabb11898
            "sp6JS7f14BuwFY8Mwkf7cFhF1uuxx",  // 25. 0xabb11898
            "sp6JS7f14BuwFY8MwmCK2un99wb4e",  // 26. 0xabb11898
            "sp6JS7f14BuwFY8MwmETztNHYu2Bx",  // 27. 0xabb11898
            "sp6JS7f14BuwFY8MwmJws9UwRASfR",  // 28. 0xabb11898
            "sp6JS7f14BuwFY8MwoH5PQkGK8tEb",  // 29. 0xabb11898
            "sp6JS7f14BuwFY8MwoVXtP2yCzjJV",  // 30. 0xabb11898
            "sp6JS7f14BuwFY8MwobxRXA9vsTeX",  // 31. 0xabb11898
            "sp6JS7f14BuwFY8Mwos3pc5Gb3ihU",  // 32. 0xabb11898
        };

        // These seeds fill the first entries of the initial page with
        // equivalent NFTs.  The split should keep these together.
        static std::initializer_list<std::string_view const> const seventeenLo{
            // These 17 need to be kept together by the implementation.
            "sp6JS7f14BuwFY8Mw5EYu5z86hKDL",  //  0. 0x399187e9
            "sp6JS7f14BuwFY8Mw5PUAMwc5ygd7",  //  1. 0x399187e9
            "sp6JS7f14BuwFY8Mw5R3xUBcLSeTs",  //  2. 0x399187e9
            "sp6JS7f14BuwFY8Mw5W6oS5sdC3oF",  //  3. 0x399187e9
            "sp6JS7f14BuwFY8Mw5pYc3D9iuLcw",  //  4. 0x399187e9
            "sp6JS7f14BuwFY8Mw5pfGVnhcdp3b",  //  5. 0x399187e9
            "sp6JS7f14BuwFY8Mw6jS6RdEqXqrN",  //  6. 0x399187e9
            "sp6JS7f14BuwFY8Mw6krt6AKbvRXW",  //  7. 0x399187e9
            "sp6JS7f14BuwFY8Mw6mnVBQq7cAN2",  //  8. 0x399187e9
            "sp6JS7f14BuwFY8Mw8ECJxPjmkufQ",  //  9. 0x399187e9
            "sp6JS7f14BuwFY8Mw8asgzcceGWYm",  // 10. 0x399187e9
            "sp6JS7f14BuwFY8MwF6J3FXnPCgL8",  // 11. 0x399187e9
            "sp6JS7f14BuwFY8MwFEud2w5czv5q",  // 12. 0x399187e9
            "sp6JS7f14BuwFY8MwFNxKVqJnx8P5",  // 13. 0x399187e9
            "sp6JS7f14BuwFY8MwFnTCXg3eRidL",  // 14. 0x399187e9
            "sp6JS7f14BuwFY8Mwj47hv1vrDge6",  // 15. 0x399187e9
            "sp6JS7f14BuwFY8Mwj6TYekeeyukh",  // 16. 0x399187e9

            // These 16 need to be kept together by the implementation.
            "sp6JS7f14BuwFY8MwjYa5yLkgCLuT",  // 17. 0xabb11898
            "sp6JS7f14BuwFY8MwjenxuJ3TH2Bc",  // 18. 0xabb11898
            "sp6JS7f14BuwFY8MwjriN7Ui11NzB",  // 19. 0xabb11898
            "sp6JS7f14BuwFY8Mwk3AuoJNSEo34",  // 20. 0xabb11898
            "sp6JS7f14BuwFY8MwkT36hnRv8hTo",  // 21. 0xabb11898
            "sp6JS7f14BuwFY8MwkTQixEXfi1Cr",  // 22. 0xabb11898
            "sp6JS7f14BuwFY8MwkYJaZM1yTJBF",  // 23. 0xabb11898
            "sp6JS7f14BuwFY8Mwkc4k1uo85qp2",  // 24. 0xabb11898
            "sp6JS7f14BuwFY8Mwkf7cFhF1uuxx",  // 25. 0xabb11898
            "sp6JS7f14BuwFY8MwmCK2un99wb4e",  // 26. 0xabb11898
            "sp6JS7f14BuwFY8MwmETztNHYu2Bx",  // 27. 0xabb11898
            "sp6JS7f14BuwFY8MwmJws9UwRASfR",  // 28. 0xabb11898
            "sp6JS7f14BuwFY8MwoH5PQkGK8tEb",  // 29. 0xabb11898
            "sp6JS7f14BuwFY8MwoVXtP2yCzjJV",  // 30. 0xabb11898
            "sp6JS7f14BuwFY8MwobxRXA9vsTeX",  // 31. 0xabb11898
            "sp6JS7f14BuwFY8Mwos3pc5Gb3ihU",  // 32. 0xabb11898
        };

        // Run the test cases.
        exerciseFixNFTokenDirV1(seventeenHi);
        exerciseFixNFTokenDirV1(seventeenLo);
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
        static std::initializer_list<std::string_view const> const seeds{
            "sp6JS7f14BuwFY8Mw5FnqmbciPvH6",  //  0. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw5MBGbyMSsXLp",  //  1. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw5S4PnDyBdKKm",  //  2. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw6kcXpM2enE35",  //  3. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw6tuuSMMwyJ44",  //  4. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw8E8JWLQ1P8pt",  //  5. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw8WwdgWkCHhEx",  //  6. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw8XDUYvU6oGhQ",  //  7. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw8ceVGL4M1zLQ",  //  8. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw8fdSwLCZWDFd",  //  9. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mw8zuF6Fg65i1E",  // 10. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwF2k7bihVfqes",  // 11. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwF6X24WXGn557",  // 12. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwFMpn7strjekg",  // 13. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwFSdy9sYVrwJs",  // 14. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwFdMcLy9UkrXn",  // 15. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwFdbwFm1AAboa",  // 16. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwFdr5AhKThVtU",  // 17. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwjFc3Q9YatvAw",  // 18. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwjRXcNs1ozEXn",  // 19. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwkQGUKL7v1FBt",  // 20. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mwkamsoxx1wECt",  // 21. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mwm3hus1dG6U8y",  // 22. 0x9a8ebed3
            "sp6JS7f14BuwFY8Mwm589M8vMRpXF",  // 23. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwmJTRJ4Fqz1A3",  // 24. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwmRfy8fer4QbL",  // 25. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwmkkFx1HtgWRx",  // 26. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwmwP9JFdKa4PS",  // 27. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwoXWJLB3ciHfo",  // 28. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwoYc1gTtT2mWL",  // 29. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwogXtHH7FNVoo",  // 30. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwoqYoA9P8gf3r",  // 31. 0x9a8ebed3
            "sp6JS7f14BuwFY8MwoujwMJofGnsA",  // 32. 0x9a8ebed3
        };

        // Create accounts for all of the seeds and fund those accounts.
        std::vector<Account> accounts;
        accounts.reserve(seeds.size());
        for (std::string_view const& seed : seeds)
        {
            Account const& account =
                accounts.emplace_back(Account::base58Seed, std::string(seed));
            env.fund(XRP(10000), account);

            // Do not close the ledger inside the loop.  If
            // fixNFTokenRemint is enabled and accounts are initialized
            // at different ledgers, they will have different account
            // sequences.  That would cause the accounts to have
            // different NFTokenID sequence numbers.
        }
        env.close();

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

        // Remove one NFT and offer from the vectors.  This offer is the one
        // that will overflow the page.
        nftIDs.pop_back();
        uint256 const offerForPageOverflow = offers.back();
        offers.pop_back();

        // buyer accepts all of the offers but one.
        for (uint256 const& offer : offers)
        {
            env(token::acceptSellOffer(buyer, offer));
            env.close();
        }

        // buyer accepts the last offer which causes a page overflow.
        env(token::acceptSellOffer(buyer, offerForPageOverflow),
            ter(tecNO_SUITABLE_NFTOKEN_PAGE));

        // Verify that all expected NFTs are owned by buyer and findable in
        // the ledger by having buyer create sell offers for all of their NFTs.
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

        // Show that Without fixNFTokenDirV1 no more NFTs can be added to
        // buyer.  Also show that fixNFTokenDirV1 fixes the problem.
        TER const expect = features[fixNFTokenDirV1]
            ? static_cast<TER>(tesSUCCESS)
            : static_cast<TER>(tecNO_SUITABLE_NFTOKEN_PAGE);
        env(token::mint(buyer, 0), txflags(tfTransferable), ter(expect));
        env.close();
    }

    void
    testConsecutivePacking(FeatureBitset features)
    {
        // We'll make a worst case scenario for NFT packing:
        //
        //  1. 33 accounts with identical low-32 bits mint 7 consecutive NFTs.
        //  2. The taxon is manipulated to always be stored as zero.
        //  3. A single account buys all 7x32 of the 33 NFTs.
        //
        // All of the NFTs should be acquired by the buyer.
        //
        // Lastly, none of the remaining NFTs should be acquirable by the
        // buyer.  They would cause page overflow.

        // This test collapses in a heap if fixNFTokenDirV1 is not enabled.
        // If it is enabled just return so we skip the test.
        if (!features[fixNFTokenDirV1])
            return;

        testcase("NFToken consecutive packing");

        using namespace test::jtx;

        Env env{*this, features};

        // Eventually all of the NFTokens will be owned by buyer.
        Account const buyer{"buyer"};
        env.fund(XRP(10000), buyer);
        env.close();

        // Here are 33 seeds that produce identical low 32-bits in their
        // corresponding AccountIDs.
        static std::initializer_list<std::string_view const> const seeds{
            "sp6JS7f14BuwFY8Mw56vZeiBuhePx",  //  0. 0x115d0525
            "sp6JS7f14BuwFY8Mw5BodF9tGuTUe",  //  1. 0x115d0525
            "sp6JS7f14BuwFY8Mw5EnhC1cg84J7",  //  2. 0x115d0525
            "sp6JS7f14BuwFY8Mw5P913Cunr2BK",  //  3. 0x115d0525
            "sp6JS7f14BuwFY8Mw5Pru7eLo1XzT",  //  4. 0x115d0525
            "sp6JS7f14BuwFY8Mw61SLUC8UX2m8",  //  5. 0x115d0525
            "sp6JS7f14BuwFY8Mw6AsBF9TpeMpq",  //  6. 0x115d0525
            "sp6JS7f14BuwFY8Mw84XqrBZkU2vE",  //  7. 0x115d0525
            "sp6JS7f14BuwFY8Mw89oSU6dBk3KB",  //  8. 0x115d0525
            "sp6JS7f14BuwFY8Mw89qUKCyDmyzj",  //  9. 0x115d0525
            "sp6JS7f14BuwFY8Mw8GfqQ9VRZ8tm",  // 10. 0x115d0525
            "sp6JS7f14BuwFY8Mw8LtW3VqrqMks",  // 11. 0x115d0525
            "sp6JS7f14BuwFY8Mw8ZrAkJc2sHew",  // 12. 0x115d0525
            "sp6JS7f14BuwFY8Mw8jpkYSNrD3ah",  // 13. 0x115d0525
            "sp6JS7f14BuwFY8MwF2mshd786m3V",  // 14. 0x115d0525
            "sp6JS7f14BuwFY8MwFHfXq9x5NbPY",  // 15. 0x115d0525
            "sp6JS7f14BuwFY8MwFrjWq5LAB8NT",  // 16. 0x115d0525
            "sp6JS7f14BuwFY8Mwj4asgSh6hQZd",  // 17. 0x115d0525
            "sp6JS7f14BuwFY8Mwj7ipFfqBSRrE",  // 18. 0x115d0525
            "sp6JS7f14BuwFY8MwjHqtcvGav8uW",  // 19. 0x115d0525
            "sp6JS7f14BuwFY8MwjLp4sk5fmzki",  // 20. 0x115d0525
            "sp6JS7f14BuwFY8MwjioHuYb3Ytkx",  // 21. 0x115d0525
            "sp6JS7f14BuwFY8MwkRjHPXWi7fGN",  // 22. 0x115d0525
            "sp6JS7f14BuwFY8MwkdVdPV3LjNN1",  // 23. 0x115d0525
            "sp6JS7f14BuwFY8MwkxUtVY5AXZFk",  // 24. 0x115d0525
            "sp6JS7f14BuwFY8Mwm4jQzdfTbY9F",  // 25. 0x115d0525
            "sp6JS7f14BuwFY8MwmCucYAqNp4iF",  // 26. 0x115d0525
            "sp6JS7f14BuwFY8Mwo2bgdFtxBzpF",  // 27. 0x115d0525
            "sp6JS7f14BuwFY8MwoGwD7v4U6qBh",  // 28. 0x115d0525
            "sp6JS7f14BuwFY8MwoUczqFADMoXi",  // 29. 0x115d0525
            "sp6JS7f14BuwFY8MwoY1xZeGd3gAr",  // 30. 0x115d0525
            "sp6JS7f14BuwFY8MwomVCbfkv4kYZ",  // 31. 0x115d0525
            "sp6JS7f14BuwFY8MwoqbrPSr4z13F",  // 32. 0x115d0525
        };

        // Create accounts for all of the seeds and fund those accounts.
        std::vector<Account> accounts;
        accounts.reserve(seeds.size());
        for (std::string_view const& seed : seeds)
        {
            Account const& account =
                accounts.emplace_back(Account::base58Seed, std::string(seed));
            env.fund(XRP(10000), account);

            // Do not close the ledger inside the loop.  If
            // fixNFTokenRemint is enabled and accounts are initialized
            // at different ledgers, they will have different account
            // sequences.  That would cause the accounts to have
            // different NFTokenID sequence numbers.
        }
        env.close();

        // All of the accounts create seven consecutive NFTs and and offer
        // those NFTs to buyer.
        std::array<std::vector<uint256>, 7> nftIDsByPage;
        for (auto& vec : nftIDsByPage)
            vec.reserve(accounts.size());
        std::array<std::vector<uint256>, 7> offers;
        for (auto& vec : offers)
            vec.reserve(accounts.size());
        for (std::size_t i = 0; i < nftIDsByPage.size(); ++i)
        {
            for (Account const& account : accounts)
            {
                // Mint the NFT.  Tweak the taxon so zero is always stored.
                std::uint32_t taxon =
                    toUInt32(nft::cipheredTaxon(i, nft::toTaxon(0)));

                uint256 const& nftID = nftIDsByPage[i].emplace_back(
                    token::getNextID(env, account, taxon, tfTransferable));
                env(token::mint(account, taxon), txflags(tfTransferable));
                env.close();

                // Create an offer to give the NFT to buyer for free.
                offers[i].emplace_back(
                    keylet::nftoffer(account, env.seq(account)).key);
                env(token::createOffer(account, nftID, XRP(0)),
                    token::destination(buyer),
                    txflags((tfSellNFToken)));
            }
        }
        env.close();

        // Verify that the low 96 bits of all generated NFTs of the same
        // sequence is identical.
        for (auto const& vec : nftIDsByPage)
        {
            uint256 const expectLowBits = vec.front() & nft::pageMask;
            for (uint256 const& nftID : vec)
            {
                BEAST_EXPECT(expectLowBits == (nftID & nft::pageMask));
            }
        }

        // Remove one NFT and offer from each of the vectors.  These offers
        // are the ones that will overflow the page.
        std::vector<uint256> overflowNFTs;
        overflowNFTs.reserve(nftIDsByPage.size());
        std::vector<uint256> overflowOffers;
        overflowOffers.reserve(nftIDsByPage.size());

        for (std::size_t i = 0; i < nftIDsByPage.size(); ++i)
        {
            overflowNFTs.push_back(nftIDsByPage[i].back());
            nftIDsByPage[i].pop_back();
            BEAST_EXPECT(nftIDsByPage[i].size() == seeds.size() - 1);

            overflowOffers.push_back(offers[i].back());
            offers[i].pop_back();
            BEAST_EXPECT(offers[i].size() == seeds.size() - 1);
        }

        // buyer accepts all of the offers that won't cause an overflow.
        // Fill the center and outsides first to exercise different boundary
        // cases.
        for (int i : std::initializer_list<int>{3, 6, 0, 1, 2, 5, 4})
        {
            for (uint256 const& offer : offers[i])
            {
                env(token::acceptSellOffer(buyer, offer));
                env.close();
            }
        }

        // buyer accepts the seven offers that would cause page overflows if
        // the transaction succeeded.
        for (uint256 const& offer : overflowOffers)
        {
            env(token::acceptSellOffer(buyer, offer),
                ter(tecNO_SUITABLE_NFTOKEN_PAGE));
            env.close();
        }

        // Verify that all expected NFTs are owned by buyer and findable in
        // the ledger by having buyer create sell offers for all of their NFTs.
        // Attempting to sell an offer that the ledger can't find generates
        // a non-tesSUCCESS error code.
        for (auto const& vec : nftIDsByPage)
        {
            for (uint256 const& nftID : vec)
            {
                env(token::createOffer(buyer, nftID, XRP(100)),
                    txflags(tfSellNFToken));
                env.close();
            }
        }

        // See what the account_objects command does with "nft_offer".
        {
            Json::Value ownedNftOffers(Json::arrayValue);
            std::string marker;
            do
            {
                Json::Value buyerOffers = [&env, &buyer, &marker]() {
                    Json::Value params;
                    params[jss::account] = buyer.human();
                    params[jss::type] = jss::nft_offer;

                    if (!marker.empty())
                        params[jss::marker] = marker;
                    return env.rpc(
                        "json", "account_objects", to_string(params));
                }();

                marker.clear();
                if (buyerOffers.isMember(jss::result))
                {
                    Json::Value& result = buyerOffers[jss::result];

                    if (result.isMember(jss::marker))
                        marker = result[jss::marker].asString();

                    if (result.isMember(jss::account_objects))
                    {
                        Json::Value& someOffers = result[jss::account_objects];
                        for (std::size_t i = 0; i < someOffers.size(); ++i)
                            ownedNftOffers.append(someOffers[i]);
                    }
                }
            } while (!marker.empty());

            // Verify there are as many offers are there are NFTs.
            {
                std::size_t totalOwnedNFTs = 0;
                for (auto const& vec : nftIDsByPage)
                    totalOwnedNFTs += vec.size();
                BEAST_EXPECT(ownedNftOffers.size() == totalOwnedNFTs);
            }

            // Cancel all the offers.
            {
                std::vector<uint256> cancelOffers;
                cancelOffers.reserve(ownedNftOffers.size());

                for (auto const& offer : ownedNftOffers)
                {
                    if (offer.isMember(jss::index))
                    {
                        uint256 offerIndex;
                        if (offerIndex.parseHex(offer[jss::index].asString()))
                            cancelOffers.push_back(offerIndex);
                    }
                }
                env(token::cancelOffer(buyer, cancelOffers));
                env.close();
            }

            // account_objects should no longer return any "nft_offer"s.
            Json::Value remainingOffers = [&env, &buyer]() {
                Json::Value params;
                params[jss::account] = buyer.human();
                params[jss::type] = jss::nft_offer;

                return env.rpc("json", "account_objects", to_string(params));
            }();
            BEAST_EXPECT(
                remainingOffers.isMember(jss::result) &&
                remainingOffers[jss::result].isMember(jss::account_objects) &&
                remainingOffers[jss::result][jss::account_objects].size() == 0);
        }

        // Verify that the ledger reports all of the NFTs owned by buyer.
        // Use the account_nfts rpc call to get the values.
        Json::Value ownedNFTs(Json::arrayValue);
        std::string marker;
        do
        {
            Json::Value buyerNFTs = [&env, &buyer, &marker]() {
                Json::Value params;
                params[jss::account] = buyer.human();
                params[jss::type] = "state";

                if (!marker.empty())
                    params[jss::marker] = marker;
                return env.rpc("json", "account_nfts", to_string(params));
            }();

            marker.clear();
            if (buyerNFTs.isMember(jss::result))
            {
                Json::Value& result = buyerNFTs[jss::result];

                if (result.isMember(jss::marker))
                    marker = result[jss::marker].asString();

                if (result.isMember(jss::account_nfts))
                {
                    Json::Value& someNFTs = result[jss::account_nfts];
                    for (std::size_t i = 0; i < someNFTs.size(); ++i)
                        ownedNFTs.append(someNFTs[i]);
                }
            }
        } while (!marker.empty());

        // Copy all of the nftIDs into a set to make validation easier.
        std::set<uint256> allNftIDs;
        for (auto& vec : nftIDsByPage)
            allNftIDs.insert(vec.begin(), vec.end());

        BEAST_EXPECT(ownedNFTs.size() == allNftIDs.size());

        for (Json::Value const& ownedNFT : ownedNFTs)
        {
            if (ownedNFT.isMember(sfNFTokenID.jsonName))
            {
                uint256 ownedID;
                BEAST_EXPECT(ownedID.parseHex(
                    ownedNFT[sfNFTokenID.jsonName].asString()));
                auto const foundIter = allNftIDs.find(ownedID);

                // Assuming we find the NFT, erase it so we know it's been found
                // and can't be found again.
                if (BEAST_EXPECT(foundIter != allNftIDs.end()))
                    allNftIDs.erase(foundIter);
            }
        }

        // All NFTs should now be accounted for, so allNftIDs should be empty.
        BEAST_EXPECT(allNftIDs.empty());
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testConsecutiveNFTs(features);
        testLopsidedSplits(features);
        testFixNFTokenDirV1(features);
        testTooManyEquivalent(features);
        testConsecutivePacking(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const fixNFTDir{
            fixNFTokenDirV1, featureNonFungibleTokensV1_1};

        testWithFeats(all - fixNFTDir - fixNFTokenRemint);
        testWithFeats(all - fixNFTokenRemint);
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(NFTokenDir, tx, ripple, 1);

}  // namespace ripple

// Seed that produces an account with the low-32 bits == 0xFFFFFFFF in
// case it is needed for future testing:
//
//   sp6JS7f14BuwFY8MwFe95Vpi9Znjs
//

// Sets of related accounts.
//
// Identifying the seeds of accounts that generate account IDs with the
// same low 32 bits takes a while.  However several sets of accounts with
// that relationship have been located.  In case these sets of accounts are
// needed for future testing scenarios they are recorded below.
#if 0
34 account seeds that produce account IDs with low 32-bits 0x399187e9:
  sp6JS7f14BuwFY8Mw5EYu5z86hKDL
  sp6JS7f14BuwFY8Mw5PUAMwc5ygd7
  sp6JS7f14BuwFY8Mw5R3xUBcLSeTs
  sp6JS7f14BuwFY8Mw5W6oS5sdC3oF
  sp6JS7f14BuwFY8Mw5pYc3D9iuLcw
  sp6JS7f14BuwFY8Mw5pfGVnhcdp3b
  sp6JS7f14BuwFY8Mw6jS6RdEqXqrN
  sp6JS7f14BuwFY8Mw6krt6AKbvRXW
  sp6JS7f14BuwFY8Mw6mnVBQq7cAN2
  sp6JS7f14BuwFY8Mw8ECJxPjmkufQ
  sp6JS7f14BuwFY8Mw8asgzcceGWYm
  sp6JS7f14BuwFY8MwF6J3FXnPCgL8
  sp6JS7f14BuwFY8MwFEud2w5czv5q
  sp6JS7f14BuwFY8MwFNxKVqJnx8P5
  sp6JS7f14BuwFY8MwFnTCXg3eRidL
  sp6JS7f14BuwFY8Mwj47hv1vrDge6
  sp6JS7f14BuwFY8Mwj6TYekeeyukh
  sp6JS7f14BuwFY8MwjFjsRDerz7jb
  sp6JS7f14BuwFY8Mwjrj9mHTLBrcX
  sp6JS7f14BuwFY8MwkKcJi3zMzAea
  sp6JS7f14BuwFY8MwkYTDdnYRm9z4
  sp6JS7f14BuwFY8Mwkq8ei4D8uPNd
  sp6JS7f14BuwFY8Mwm2pFruxbnJRd
  sp6JS7f14BuwFY8MwmJV2ZnAjpC2g
  sp6JS7f14BuwFY8MwmTFMPHQHfVYF
  sp6JS7f14BuwFY8MwmkG2jXEgqiud
  sp6JS7f14BuwFY8Mwms3xEh5tMDTw
  sp6JS7f14BuwFY8MwmtipW4D8giZ9
  sp6JS7f14BuwFY8MwoRQBZm4KUUeE
  sp6JS7f14BuwFY8MwoVey94QpXcrc
  sp6JS7f14BuwFY8MwoZiuUoUTo3VG
  sp6JS7f14BuwFY8MwonFFDLT4bHAZ
  sp6JS7f14BuwFY8MwooGphD4hefBQ
  sp6JS7f14BuwFY8MwoxDp3dmX6q5N

34 account seeds that produce account IDs with low 32-bits 0x473f2c9a:
  sp6JS7f14BuwFY8Mw53ktgqmv5Bmz
  sp6JS7f14BuwFY8Mw5KPb2Kz7APFX
  sp6JS7f14BuwFY8Mw5Xx4A6HRTPEE
  sp6JS7f14BuwFY8Mw5y6qZFNAo358
  sp6JS7f14BuwFY8Mw6kdaBg1QrZfn
  sp6JS7f14BuwFY8Mw8QmTfLMAZ5K1
  sp6JS7f14BuwFY8Mw8cbRRVcCEELr
  sp6JS7f14BuwFY8Mw8gQvJebmxvDG
  sp6JS7f14BuwFY8Mw8qPQurwu3P7Y
  sp6JS7f14BuwFY8MwFS4PEVKmuPy5
  sp6JS7f14BuwFY8MwFUQM1rAsQ8tS
  sp6JS7f14BuwFY8MwjJBZCkuwsRnM
  sp6JS7f14BuwFY8MwjTdS8vZhX5E9
  sp6JS7f14BuwFY8MwjhSmWCbNhd25
  sp6JS7f14BuwFY8MwjwkpqwZsDBw9
  sp6JS7f14BuwFY8MwjyET4p6eqd5J
  sp6JS7f14BuwFY8MwkMNAe4JhnG7E
  sp6JS7f14BuwFY8MwkRRpnT93UWWS
  sp6JS7f14BuwFY8MwkY9CvB22RvUe
  sp6JS7f14BuwFY8Mwkhw9VxXqmTr7
  sp6JS7f14BuwFY8MwkmgaTat7eFa7
  sp6JS7f14BuwFY8Mwkq5SxGGv1oLH
  sp6JS7f14BuwFY8MwmCBM5p5bTg6y
  sp6JS7f14BuwFY8MwmmmXaVah64dB
  sp6JS7f14BuwFY8Mwo7R7Cn614v9V
  sp6JS7f14BuwFY8MwoCAG1na7GR2M
  sp6JS7f14BuwFY8MwoDuPvJS4gG7C
  sp6JS7f14BuwFY8MwoMMowSyPQLfy
  sp6JS7f14BuwFY8MwoRqDiwTNsTBm
  sp6JS7f14BuwFY8MwoWbBWtjpB7pg
  sp6JS7f14BuwFY8Mwoi1AEeELGecF
  sp6JS7f14BuwFY8MwopGP6Lo5byuj
  sp6JS7f14BuwFY8MwoufkXGHp2VW8
  sp6JS7f14BuwFY8MwowGeagFQY32k

34 account seeds that produce account IDs with low 32-bits 0x4d59f0d1:
  sp6JS7f14BuwFY8Mw5CsNgH64zxK7
  sp6JS7f14BuwFY8Mw5Dg4wi2E344h
  sp6JS7f14BuwFY8Mw5ErV949Zh2PX
  sp6JS7f14BuwFY8Mw5p4nsQvEUE1s
  sp6JS7f14BuwFY8Mw8LGnkbaP68Gn
  sp6JS7f14BuwFY8Mw8aq6RCBc3iHo
  sp6JS7f14BuwFY8Mw8bkWaGoKYT6e
  sp6JS7f14BuwFY8Mw8qrCuXnzAXVj
  sp6JS7f14BuwFY8MwFDKcPAHPHJTm
  sp6JS7f14BuwFY8MwFUXJs4unfgNu
  sp6JS7f14BuwFY8MwFj9Yv5LjshD9
  sp6JS7f14BuwFY8Mwj3H73nmq5UaC
  sp6JS7f14BuwFY8MwjHSYShis1Yhk
  sp6JS7f14BuwFY8MwjpfE1HVo8UP1
  sp6JS7f14BuwFY8Mwk6JE1SXUuiNc
  sp6JS7f14BuwFY8MwkASgxEjEnFmU
  sp6JS7f14BuwFY8MwkGNY8kg7R6RK
  sp6JS7f14BuwFY8MwkHinNZ8SYBQu
  sp6JS7f14BuwFY8MwkXLCW1hbhGya
  sp6JS7f14BuwFY8MwkZ7mWrYK9YtU
  sp6JS7f14BuwFY8MwkdFSqNB5DbKL
  sp6JS7f14BuwFY8Mwm3jdBaCAx8H6
  sp6JS7f14BuwFY8Mwm3rk5hEwDRtY
  sp6JS7f14BuwFY8Mwm77a2ULuwxu4
  sp6JS7f14BuwFY8MwmJpY7braKLaN
  sp6JS7f14BuwFY8MwmKHQjG4XiZ6g
  sp6JS7f14BuwFY8Mwmmv8Y3wyUDzs
  sp6JS7f14BuwFY8MwmucFe1WgqtwG
  sp6JS7f14BuwFY8Mwo1EjdU1bznZR
  sp6JS7f14BuwFY8MwoJiqankkU5uR
  sp6JS7f14BuwFY8MwoLnvQ6zdqbKw
  sp6JS7f14BuwFY8MwoUGeJ319eu48
  sp6JS7f14BuwFY8MwoYf135tQjHP4
  sp6JS7f14BuwFY8MwogeF6M6SAyid

34 account seeds that produce account IDs with low 32-bits 0xabb11898:
  sp6JS7f14BuwFY8Mw5DgiYaNVSb1G
  sp6JS7f14BuwFY8Mw5k6e94TMvuox
  sp6JS7f14BuwFY8Mw5tTSN7KzYxiT
  sp6JS7f14BuwFY8Mw61XV6m33utif
  sp6JS7f14BuwFY8Mw87jKfrjiENCb
  sp6JS7f14BuwFY8Mw8AFtxxFiRtJG
  sp6JS7f14BuwFY8Mw8cosAVExzbeE
  sp6JS7f14BuwFY8Mw8fmkQ63zE8WQ
  sp6JS7f14BuwFY8Mw8iYSsxNbDN6D
  sp6JS7f14BuwFY8Mw8wTZdGRJyyM1
  sp6JS7f14BuwFY8Mw8z7xEh3qBGr7
  sp6JS7f14BuwFY8MwFL5gpKQWZj7g
  sp6JS7f14BuwFY8MwFPeZchXQnRZ5
  sp6JS7f14BuwFY8MwFSPxWSJVoU29
  sp6JS7f14BuwFY8MwFYyVkqX8kvRm
  sp6JS7f14BuwFY8MwFcbVikUEwJvk
  sp6JS7f14BuwFY8MwjF7NcZk1NctK
  sp6JS7f14BuwFY8MwjJCwYr9zSfAv
  sp6JS7f14BuwFY8MwjYa5yLkgCLuT
  sp6JS7f14BuwFY8MwjenxuJ3TH2Bc
  sp6JS7f14BuwFY8MwjriN7Ui11NzB
  sp6JS7f14BuwFY8Mwk3AuoJNSEo34
  sp6JS7f14BuwFY8MwkT36hnRv8hTo
  sp6JS7f14BuwFY8MwkTQixEXfi1Cr
  sp6JS7f14BuwFY8MwkYJaZM1yTJBF
  sp6JS7f14BuwFY8Mwkc4k1uo85qp2
  sp6JS7f14BuwFY8Mwkf7cFhF1uuxx
  sp6JS7f14BuwFY8MwmCK2un99wb4e
  sp6JS7f14BuwFY8MwmETztNHYu2Bx
  sp6JS7f14BuwFY8MwmJws9UwRASfR
  sp6JS7f14BuwFY8MwoH5PQkGK8tEb
  sp6JS7f14BuwFY8MwoVXtP2yCzjJV
  sp6JS7f14BuwFY8MwobxRXA9vsTeX
  sp6JS7f14BuwFY8Mwos3pc5Gb3ihU

34 account seeds that produce account IDs with low 32-bits 0xce627322:
  sp6JS7f14BuwFY8Mw5Ck6i83pGNh3
  sp6JS7f14BuwFY8Mw5FKuwTxjAdH1
  sp6JS7f14BuwFY8Mw5FVKkEn6TkLH
  sp6JS7f14BuwFY8Mw5NbQwLwHDd5v
  sp6JS7f14BuwFY8Mw5X1dbz3msZaZ
  sp6JS7f14BuwFY8Mw6qv6qaXNeP74
  sp6JS7f14BuwFY8Mw81SXagUeutCw
  sp6JS7f14BuwFY8Mw84Ph7Qa8kwwk
  sp6JS7f14BuwFY8Mw8Hp4gFyU3Qko
  sp6JS7f14BuwFY8Mw8Kt8bAKredSx
  sp6JS7f14BuwFY8Mw8XHK3VKRQ7v7
  sp6JS7f14BuwFY8Mw8eGyWxZGHY6v
  sp6JS7f14BuwFY8Mw8iU5CLyHVcD2
  sp6JS7f14BuwFY8Mw8u3Zr26Ar914
  sp6JS7f14BuwFY8MwF2Kcdxtjzjv8
  sp6JS7f14BuwFY8MwFLmPWb6rbxNg
  sp6JS7f14BuwFY8MwFUu8s7UVuxuJ
  sp6JS7f14BuwFY8MwFYBaatwHxAJ8
  sp6JS7f14BuwFY8Mwjg6hFkeHwoqG
  sp6JS7f14BuwFY8MwjjycJojy2ufk
  sp6JS7f14BuwFY8MwkEWoxcSKGPXv
  sp6JS7f14BuwFY8MwkMe7wLkEUsQT
  sp6JS7f14BuwFY8MwkvyKLaPUc4FS
  sp6JS7f14BuwFY8Mwm8doqXPKZmVQ
  sp6JS7f14BuwFY8Mwm9r3No8yQ8Tx
  sp6JS7f14BuwFY8Mwm9w6dks68W9B
  sp6JS7f14BuwFY8MwmMPrv9sCdbpS
  sp6JS7f14BuwFY8MwmPAvs3fcQNja
  sp6JS7f14BuwFY8MwmS5jasapfcnJ
  sp6JS7f14BuwFY8MwmU2L3qJEhnuA
  sp6JS7f14BuwFY8MwoAQYmiBnW7fM
  sp6JS7f14BuwFY8MwoBkkkXrPmkKF
  sp6JS7f14BuwFY8MwonfmxPo6tkvC
  sp6JS7f14BuwFY8MwouZFwhiNcYq6

34 account seeds that produce account IDs with low 32-bits 0xe29643e8:
  sp6JS7f14BuwFY8Mw5EfAavcXAh2k
  sp6JS7f14BuwFY8Mw5LhFjLkFSCVF
  sp6JS7f14BuwFY8Mw5bRfEv5HgdBh
  sp6JS7f14BuwFY8Mw5d6sPcKzypKN
  sp6JS7f14BuwFY8Mw5rcqDtk1fACP
  sp6JS7f14BuwFY8Mw5xkxRq1Notzv
  sp6JS7f14BuwFY8Mw66fbkdw5WYmt
  sp6JS7f14BuwFY8Mw6diEG8sZ7Fx7
  sp6JS7f14BuwFY8Mw6v2r1QhG7xc1
  sp6JS7f14BuwFY8Mw6zP6DHCTx2Fd
  sp6JS7f14BuwFY8Mw8B3n39JKuFkk
  sp6JS7f14BuwFY8Mw8FmBvqYw7uqn
  sp6JS7f14BuwFY8Mw8KEaftb1eRwu
  sp6JS7f14BuwFY8Mw8WJ1qKkegj9N
  sp6JS7f14BuwFY8Mw8r8cAZEkq2BS
  sp6JS7f14BuwFY8MwFKPxxwF65gZh
  sp6JS7f14BuwFY8MwFKhaF8APcN5H
  sp6JS7f14BuwFY8MwFN2buJn4BgYC
  sp6JS7f14BuwFY8MwFUTe175MjP3x
  sp6JS7f14BuwFY8MwFZhmRDb53NNb
  sp6JS7f14BuwFY8MwFa2Azn5nU2WS
  sp6JS7f14BuwFY8MwjNNt91hwgkn7
  sp6JS7f14BuwFY8MwjdiYt6ChACe7
  sp6JS7f14BuwFY8Mwk5qFVQ48Mmr9
  sp6JS7f14BuwFY8MwkGvCj7pNf1zG
  sp6JS7f14BuwFY8MwkY9UcN2D2Fzs
  sp6JS7f14BuwFY8MwkpGvSk9G9RyT
  sp6JS7f14BuwFY8MwmGQ7nJf1eEzV
  sp6JS7f14BuwFY8MwmQLjGsYdyAmV
  sp6JS7f14BuwFY8MwmZ8usztKvikT
  sp6JS7f14BuwFY8MwobyMLC2hQdFR
  sp6JS7f14BuwFY8MwoiRtwUecZeJ5
  sp6JS7f14BuwFY8MwojHjKsUzj1KJ
  sp6JS7f14BuwFY8Mwop29anGAjidU

33 account seeds that produce account IDs with low 32-bits 0x115d0525:
  sp6JS7f14BuwFY8Mw56vZeiBuhePx
  sp6JS7f14BuwFY8Mw5BodF9tGuTUe
  sp6JS7f14BuwFY8Mw5EnhC1cg84J7
  sp6JS7f14BuwFY8Mw5P913Cunr2BK
  sp6JS7f14BuwFY8Mw5Pru7eLo1XzT
  sp6JS7f14BuwFY8Mw61SLUC8UX2m8
  sp6JS7f14BuwFY8Mw6AsBF9TpeMpq
  sp6JS7f14BuwFY8Mw84XqrBZkU2vE
  sp6JS7f14BuwFY8Mw89oSU6dBk3KB
  sp6JS7f14BuwFY8Mw89qUKCyDmyzj
  sp6JS7f14BuwFY8Mw8GfqQ9VRZ8tm
  sp6JS7f14BuwFY8Mw8LtW3VqrqMks
  sp6JS7f14BuwFY8Mw8ZrAkJc2sHew
  sp6JS7f14BuwFY8Mw8jpkYSNrD3ah
  sp6JS7f14BuwFY8MwF2mshd786m3V
  sp6JS7f14BuwFY8MwFHfXq9x5NbPY
  sp6JS7f14BuwFY8MwFrjWq5LAB8NT
  sp6JS7f14BuwFY8Mwj4asgSh6hQZd
  sp6JS7f14BuwFY8Mwj7ipFfqBSRrE
  sp6JS7f14BuwFY8MwjHqtcvGav8uW
  sp6JS7f14BuwFY8MwjLp4sk5fmzki
  sp6JS7f14BuwFY8MwjioHuYb3Ytkx
  sp6JS7f14BuwFY8MwkRjHPXWi7fGN
  sp6JS7f14BuwFY8MwkdVdPV3LjNN1
  sp6JS7f14BuwFY8MwkxUtVY5AXZFk
  sp6JS7f14BuwFY8Mwm4jQzdfTbY9F
  sp6JS7f14BuwFY8MwmCucYAqNp4iF
  sp6JS7f14BuwFY8Mwo2bgdFtxBzpF
  sp6JS7f14BuwFY8MwoGwD7v4U6qBh
  sp6JS7f14BuwFY8MwoUczqFADMoXi
  sp6JS7f14BuwFY8MwoY1xZeGd3gAr
  sp6JS7f14BuwFY8MwomVCbfkv4kYZ
  sp6JS7f14BuwFY8MwoqbrPSr4z13F

33 account seeds that produce account IDs with low 32-bits 0x304033aa:
  sp6JS7f14BuwFY8Mw5DaUP9agF5e1
  sp6JS7f14BuwFY8Mw5ohbtmPN4yGN
  sp6JS7f14BuwFY8Mw5rRsA5fcoTAQ
  sp6JS7f14BuwFY8Mw6zpYHMY3m6KT
  sp6JS7f14BuwFY8Mw86BzQq4sTnoW
  sp6JS7f14BuwFY8Mw8CCpnfvmGdV7
  sp6JS7f14BuwFY8Mw8DRjUDaBcFco
  sp6JS7f14BuwFY8Mw8cL7GPo3zZN7
  sp6JS7f14BuwFY8Mw8y6aeYVtH6qt
  sp6JS7f14BuwFY8MwFZR3PtVTCdUH
  sp6JS7f14BuwFY8MwFcdcdbgz7m3s
  sp6JS7f14BuwFY8MwjdnJDiUxEBRR
  sp6JS7f14BuwFY8MwjhxWgSntqrFe
  sp6JS7f14BuwFY8MwjrSHEhZ8CUM1
  sp6JS7f14BuwFY8MwjzkEeSTc9ZYf
  sp6JS7f14BuwFY8MwkBZSk9JhaeCB
  sp6JS7f14BuwFY8MwkGfwNY4i2iiU
  sp6JS7f14BuwFY8MwknjtZd2oU2Ff
  sp6JS7f14BuwFY8Mwkszsqd3ok9NE
  sp6JS7f14BuwFY8Mwm58A81MAMvgZ
  sp6JS7f14BuwFY8MwmiPTWysuDJCH
  sp6JS7f14BuwFY8MwmxhiNeLfD76r
  sp6JS7f14BuwFY8Mwo7SPdkwpGrFH
  sp6JS7f14BuwFY8MwoANq4F1Sj3qH
  sp6JS7f14BuwFY8MwoVjcHufAkd6L
  sp6JS7f14BuwFY8MwoVxHBXdaxzhm
  sp6JS7f14BuwFY8MwoZ2oTjBNfLpm
  sp6JS7f14BuwFY8Mwoc9swzyotFVD
  sp6JS7f14BuwFY8MwogMqVRwVEcQ9
  sp6JS7f14BuwFY8MwohMm7WxwnFqH
  sp6JS7f14BuwFY8MwopUcpZHuF8BH
  sp6JS7f14BuwFY8Mwor6rW6SS7tiB
  sp6JS7f14BuwFY8MwoxyaqYz4Ngsb

33 account seeds that produce account IDs with low 32-bits 0x42d4e09c:
  sp6JS7f14BuwFY8Mw58NSZH9EaUxQ
  sp6JS7f14BuwFY8Mw5JByk1pgPpL7
  sp6JS7f14BuwFY8Mw5YrJJuXnkHVB
  sp6JS7f14BuwFY8Mw5kZe2ZzNSnKR
  sp6JS7f14BuwFY8Mw6eXHTsbwi1U7
  sp6JS7f14BuwFY8Mw6gqN7HHDDKSh
  sp6JS7f14BuwFY8Mw6zw8L1sSSR53
  sp6JS7f14BuwFY8Mw8E4WqSKKbksy
  sp6JS7f14BuwFY8MwF3V9gemqJtND
  sp6JS7f14BuwFY8Mwj4j46LHWZuY6
  sp6JS7f14BuwFY8MwjF5i8vh4Ezjy
  sp6JS7f14BuwFY8MwjJZpEKgMpUAt
  sp6JS7f14BuwFY8MwjWL7LfnzNUuh
  sp6JS7f14BuwFY8Mwk7Y1csGuqAhX
  sp6JS7f14BuwFY8MwkB1HVH17hN5W
  sp6JS7f14BuwFY8MwkBntH7BZZupu
  sp6JS7f14BuwFY8MwkEy4rMbNHG9P
  sp6JS7f14BuwFY8MwkKz4LYesZeiN
  sp6JS7f14BuwFY8MwkUrXyo9gMDPM
  sp6JS7f14BuwFY8MwkV2hySsxej1G
  sp6JS7f14BuwFY8MwkozhTVN12F9C
  sp6JS7f14BuwFY8MwkpkzGB3sFJw5
  sp6JS7f14BuwFY8Mwks3zDZLGrhdn
  sp6JS7f14BuwFY8MwktG1KCS7L2wW
  sp6JS7f14BuwFY8Mwm1jVFsafwcYx
  sp6JS7f14BuwFY8Mwm8hmrU6g5Wd6
  sp6JS7f14BuwFY8MwmFvstfRF7e2f
  sp6JS7f14BuwFY8MwmeRohi6m5fs8
  sp6JS7f14BuwFY8MwmmU96RHUaRZL
  sp6JS7f14BuwFY8MwoDFzteYqaUh4
  sp6JS7f14BuwFY8MwoPkTf5tDykPF
  sp6JS7f14BuwFY8MwoSbMaDtiMoDN
  sp6JS7f14BuwFY8MwoVL1vY1CysjR

33 account seeds that produce account IDs with low 32-bits 0x9a8ebed3:
  sp6JS7f14BuwFY8Mw5FnqmbciPvH6
  sp6JS7f14BuwFY8Mw5MBGbyMSsXLp
  sp6JS7f14BuwFY8Mw5S4PnDyBdKKm
  sp6JS7f14BuwFY8Mw6kcXpM2enE35
  sp6JS7f14BuwFY8Mw6tuuSMMwyJ44
  sp6JS7f14BuwFY8Mw8E8JWLQ1P8pt
  sp6JS7f14BuwFY8Mw8WwdgWkCHhEx
  sp6JS7f14BuwFY8Mw8XDUYvU6oGhQ
  sp6JS7f14BuwFY8Mw8ceVGL4M1zLQ
  sp6JS7f14BuwFY8Mw8fdSwLCZWDFd
  sp6JS7f14BuwFY8Mw8zuF6Fg65i1E
  sp6JS7f14BuwFY8MwF2k7bihVfqes
  sp6JS7f14BuwFY8MwF6X24WXGn557
  sp6JS7f14BuwFY8MwFMpn7strjekg
  sp6JS7f14BuwFY8MwFSdy9sYVrwJs
  sp6JS7f14BuwFY8MwFdMcLy9UkrXn
  sp6JS7f14BuwFY8MwFdbwFm1AAboa
  sp6JS7f14BuwFY8MwFdr5AhKThVtU
  sp6JS7f14BuwFY8MwjFc3Q9YatvAw
  sp6JS7f14BuwFY8MwjRXcNs1ozEXn
  sp6JS7f14BuwFY8MwkQGUKL7v1FBt
  sp6JS7f14BuwFY8Mwkamsoxx1wECt
  sp6JS7f14BuwFY8Mwm3hus1dG6U8y
  sp6JS7f14BuwFY8Mwm589M8vMRpXF
  sp6JS7f14BuwFY8MwmJTRJ4Fqz1A3
  sp6JS7f14BuwFY8MwmRfy8fer4QbL
  sp6JS7f14BuwFY8MwmkkFx1HtgWRx
  sp6JS7f14BuwFY8MwmwP9JFdKa4PS
  sp6JS7f14BuwFY8MwoXWJLB3ciHfo
  sp6JS7f14BuwFY8MwoYc1gTtT2mWL
  sp6JS7f14BuwFY8MwogXtHH7FNVoo
  sp6JS7f14BuwFY8MwoqYoA9P8gf3r
  sp6JS7f14BuwFY8MwoujwMJofGnsA

33 account seeds that produce account IDs with low 32-bits 0xa1dcea4a:
  sp6JS7f14BuwFY8Mw5Ccov2N36QTy
  sp6JS7f14BuwFY8Mw5CuSemVb5p7w
  sp6JS7f14BuwFY8Mw5Ep8wpsTfpSz
  sp6JS7f14BuwFY8Mw5WtutJc2H45M
  sp6JS7f14BuwFY8Mw6vsDeaSKeUJZ
  sp6JS7f14BuwFY8Mw83t5BPWUAzzF
  sp6JS7f14BuwFY8Mw8FYGnK35mgkV
  sp6JS7f14BuwFY8Mw8huo1x5pfKKJ
  sp6JS7f14BuwFY8Mw8mPStxfMDrZa
  sp6JS7f14BuwFY8Mw8yC3A7aQJytK
  sp6JS7f14BuwFY8MwFCWCDmo9o3t8
  sp6JS7f14BuwFY8MwFjapa4gKxPhR
  sp6JS7f14BuwFY8Mwj8CWtG29uw71
  sp6JS7f14BuwFY8MwjHyU5KpEMLVT
  sp6JS7f14BuwFY8MwjMZSN7LZuWD8
  sp6JS7f14BuwFY8Mwja2TXJNBhKHU
  sp6JS7f14BuwFY8Mwjf3xNTopHKTF
  sp6JS7f14BuwFY8Mwjn5RAhedPeuM
  sp6JS7f14BuwFY8MwkJdr4d6QoE8K
  sp6JS7f14BuwFY8MwkmBryo3SUoLm
  sp6JS7f14BuwFY8MwkrPdsc4tR8yw
  sp6JS7f14BuwFY8Mwkttjcw2a65Fi
  sp6JS7f14BuwFY8Mwm19n3rSaNx5S
  sp6JS7f14BuwFY8Mwm3ryr4Xp2aQX
  sp6JS7f14BuwFY8MwmBnDmgnJLB6B
  sp6JS7f14BuwFY8MwmHgPjzrYjthq
  sp6JS7f14BuwFY8MwmeV55DAnWKdd
  sp6JS7f14BuwFY8Mwo49hK6BGrauT
  sp6JS7f14BuwFY8Mwo56vfKY9aoWu
  sp6JS7f14BuwFY8MwoU7tTTXLQTrh
  sp6JS7f14BuwFY8MwoXpogSF2KaZB
  sp6JS7f14BuwFY8MwoY9JYQAR16pc
  sp6JS7f14BuwFY8MwoozLzKNAEXKM

33 account seeds that produce account IDs with low 32-bits 0xbd2116db:
  sp6JS7f14BuwFY8Mw5GrpkmPuA3Bw
  sp6JS7f14BuwFY8Mw5r1sLoQJZDc6
  sp6JS7f14BuwFY8Mw68zzRmezLdd6
  sp6JS7f14BuwFY8Mw6jDSyaiF1mRp
  sp6JS7f14BuwFY8Mw813wU9u5D6Uh
  sp6JS7f14BuwFY8Mw8BBvpf2JFGoJ
  sp6JS7f14BuwFY8Mw8F7zXxAiT263
  sp6JS7f14BuwFY8Mw8XG7WuVGHP2N
  sp6JS7f14BuwFY8Mw8eyWrcz91cz6
  sp6JS7f14BuwFY8Mw8yNVKFVYyk9u
  sp6JS7f14BuwFY8MwF2oA6ePqvZWP
  sp6JS7f14BuwFY8MwF9VkcSNh3keq
  sp6JS7f14BuwFY8MwFYsMWajgEf2j
  sp6JS7f14BuwFY8Mwj3Gu43jYoJ4n
  sp6JS7f14BuwFY8MwjJ5iRmYDHrW4
  sp6JS7f14BuwFY8MwjaUSSga93CiM
  sp6JS7f14BuwFY8MwjxgLh2FY4Lvt
  sp6JS7f14BuwFY8Mwk9hQdNZUgmTB
  sp6JS7f14BuwFY8MwkcMXqtFp1sMx
  sp6JS7f14BuwFY8MwkzZCDc56jsUB
  sp6JS7f14BuwFY8Mwm5Zz7fP24Qym
  sp6JS7f14BuwFY8MwmDWqizXSoJRG
  sp6JS7f14BuwFY8MwmKHmkNYdMqqi
  sp6JS7f14BuwFY8MwmRfAWHxWpGNK
  sp6JS7f14BuwFY8MwmjCdXwyhphZ1
  sp6JS7f14BuwFY8MwmmukDAm1w6FL
  sp6JS7f14BuwFY8Mwmmz2SzaR9TRH
  sp6JS7f14BuwFY8Mwmz2z5mKHXzfn
  sp6JS7f14BuwFY8Mwo2xNe5629r5k
  sp6JS7f14BuwFY8MwoKy8tZxZrfJw
  sp6JS7f14BuwFY8MwoLyQ9aMsq8Dm
  sp6JS7f14BuwFY8MwoqqYkewuyZck
  sp6JS7f14BuwFY8MwouvvhREVp6Pp

33 account seeds that produce account IDs with low 32-bits 0xd80df065:
  sp6JS7f14BuwFY8Mw5B7ERyhAfgHA
  sp6JS7f14BuwFY8Mw5VuW3cF7bm2v
  sp6JS7f14BuwFY8Mw5py3t1j7YbFT
  sp6JS7f14BuwFY8Mw5qc84SzB6RHr
  sp6JS7f14BuwFY8Mw5vGHW1G1hAy8
  sp6JS7f14BuwFY8Mw6gVa8TYukws6
  sp6JS7f14BuwFY8Mw8K9w1RoUAv1w
  sp6JS7f14BuwFY8Mw8KvKtB7787CA
  sp6JS7f14BuwFY8Mw8Y7WhRbuFzRq
  sp6JS7f14BuwFY8Mw8cipw7inRmMn
  sp6JS7f14BuwFY8MwFM5fAUNLNB13
  sp6JS7f14BuwFY8MwFSe1zAsht3X3
  sp6JS7f14BuwFY8MwFYNdigqQuHZM
  sp6JS7f14BuwFY8MwjWkejj7V4V5Q
  sp6JS7f14BuwFY8Mwjd2JGpsjvynq
  sp6JS7f14BuwFY8Mwjg1xkducn751
  sp6JS7f14BuwFY8Mwjsp6LnaJvL1W
  sp6JS7f14BuwFY8MwjvSbLc9593yH
  sp6JS7f14BuwFY8Mwjw2h5wx7U6vZ
  sp6JS7f14BuwFY8MwjxKUjtRsmPLH
  sp6JS7f14BuwFY8Mwk1Yy8ginDfqv
  sp6JS7f14BuwFY8Mwk2HrWhWwZP12
  sp6JS7f14BuwFY8Mwk4SsqiexvpWs
  sp6JS7f14BuwFY8Mwk66zCs5ACpE6
  sp6JS7f14BuwFY8MwkCwx6vY97Nwh
  sp6JS7f14BuwFY8MwknrbjnhTTWU8
  sp6JS7f14BuwFY8MwkokDy2ShRzQx
  sp6JS7f14BuwFY8Mwm3BxnRPNxsuu
  sp6JS7f14BuwFY8MwmY9EWdQQsFVr
  sp6JS7f14BuwFY8MwmYTWjrDhmk8S
  sp6JS7f14BuwFY8Mwo9skXt9Y5BVS
  sp6JS7f14BuwFY8MwoZYKZybJ1Crp
  sp6JS7f14BuwFY8MwoyXqkhySfSmF

33 account seeds that produce account IDs with low 32-bits 0xe2e44294:
  sp6JS7f14BuwFY8Mw53dmvTgNtBwi
  sp6JS7f14BuwFY8Mw5Wrxsqn6WrXW
  sp6JS7f14BuwFY8Mw5fGDT31RCXgC
  sp6JS7f14BuwFY8Mw5nKRkubwrLWM
  sp6JS7f14BuwFY8Mw5nXMajwKjriB
  sp6JS7f14BuwFY8Mw5xZybggrC9NG
  sp6JS7f14BuwFY8Mw5xea8f6dBMV5
  sp6JS7f14BuwFY8Mw5zDGofAHy5Lb
  sp6JS7f14BuwFY8Mw6eado41rQNVG
  sp6JS7f14BuwFY8Mw6yqKXQsQJPuU
  sp6JS7f14BuwFY8Mw83MSN4FDzSGH
  sp6JS7f14BuwFY8Mw8B3pUbzQqHe2
  sp6JS7f14BuwFY8Mw8WwRLnhBRvfk
  sp6JS7f14BuwFY8Mw8hDBpKbpJwJX
  sp6JS7f14BuwFY8Mw8jggRSZACe7M
  sp6JS7f14BuwFY8Mw8mJRpU3qWbwC
  sp6JS7f14BuwFY8MwFDnVozykN21u
  sp6JS7f14BuwFY8MwFGGRGY9fctgv
  sp6JS7f14BuwFY8MwjKznfChH9DQb
  sp6JS7f14BuwFY8MwjbC5GvngRCk6
  sp6JS7f14BuwFY8Mwk3Lb7FPe1629
  sp6JS7f14BuwFY8MwkCeS41BwVrBD
  sp6JS7f14BuwFY8MwkDnnvRyuWJ7d
  sp6JS7f14BuwFY8MwkbkRNnzDEFpf
  sp6JS7f14BuwFY8MwkiNhaVhGNk6v
  sp6JS7f14BuwFY8Mwm1X4UJXRZx3p
  sp6JS7f14BuwFY8Mwm7da9q5vfq7J
  sp6JS7f14BuwFY8MwmPLqfBPrHw5H
  sp6JS7f14BuwFY8MwmbJpxvVjEwm2
  sp6JS7f14BuwFY8MwoAVeA7ka37cD
  sp6JS7f14BuwFY8MwoTFFTAwFKmVM
  sp6JS7f14BuwFY8MwoYsne51VpDE3
  sp6JS7f14BuwFY8MwohLVnU1VTk5h

#endif  // 0
