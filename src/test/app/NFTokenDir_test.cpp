
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/protocol/nftPageMask.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <ostream>
#include <set>
#include <string_view>
#include <vector>

namespace xrpl {

class NFTokenDir_test : public beast::unit_test::Suite
{
    // printNFTPages is a helper function that may be used for debugging.
    //
    // It uses the ledger RPC command to show the NFT pages in the ledger.
    // This parameter controls how noisy the output is.
    enum class Volume : bool {
        Quiet = false,
        Noisy = true,
    };

    static void
    printNFTPages(test::jtx::Env& env, Volume vol)
    {
        json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::binary] = false;
        {
            json::Value jrr = env.rpc("json", "ledger_data", to_string(jvParams));

            // Iterate the state and print all NFTokenPages.
            if (!jrr.isMember(jss::result) || !jrr[jss::result].isMember(jss::state))
            {
                std::cout << "No ledger state found!" << std::endl;
                return;
            }
            json::Value& state = jrr[jss::result][jss::state];
            if (!state.isArray())
            {
                std::cout << "Ledger state is not array!" << std::endl;
                return;
            }
            for (json::UInt i = 0; i < state.size(); ++i)
            {
                if (state[i].isMember(sfNFTokens.jsonName) &&
                    state[i][sfNFTokens.jsonName].isArray())
                {
                    std::uint32_t const tokenCount = state[i][sfNFTokens.jsonName].size();
                    std::cout << tokenCount << " NFtokens in page "
                              << state[i][jss::index].asString() << std::endl;

                    if (vol == Volume::Noisy)
                    {
                        std::cout << state[i].toStyledString() << std::endl;
                    }
                    else
                    {
                        if (tokenCount > 0)
                        {
                            std::cout
                                << "first: " << state[i][sfNFTokens.jsonName][0u].toStyledString()
                                << std::endl;
                        }
                        if (tokenCount > 1)
                        {
                            std::cout
                                << "last: "
                                << state[i][sfNFTokens.jsonName][tokenCount - 1].toStyledString()
                                << std::endl;
                        }
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
        static constexpr std::size_t kNftCount = 100;
        std::vector<uint256> nftIDs;
        nftIDs.reserve(kNftCount);
        for (int i = 0; i < kNftCount; ++i)
        {
            std::uint32_t const taxon = toUInt32(nft::cipheredTaxon(i, nft::toTaxon(0)));
            nftIDs.emplace_back(token::getNextID(env, issuer, taxon, tfTransferable));
            env(token::mint(issuer, taxon), Txflags(tfTransferable));
            env.close();
        }

        // Create an offer for each of the NFTs.  This verifies that the ledger
        // can find all of the minted NFTs.
        std::vector<uint256> offers;
        for (uint256 const& nftID : nftIDs)
        {
            offers.emplace_back(keylet::nftokenOffer(issuer, env.seq(issuer)).key);
            env(token::createOffer(issuer, nftID, XRP(0)), Txflags(tfSellNFToken));
            env.close();
        }

        // Buyer accepts all of the offers in reverse order.
        std::ranges::reverse(offers);
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
        auto exerciseLopsided = [this,
                                 &features](std::initializer_list<std::string_view const> seeds) {
            Env env{*this, features};

            // Eventually all of the NFTokens will be owned by buyer.
            Account const buyer{"buyer"};
            env.fund(XRP(10000), buyer);
            env.close();

            // Create accounts for all of the seeds and fund those accounts.
            std::vector<Account> accounts;
            accounts.reserve(seeds.size());
            for (std::string_view const seed : seeds)
            {
                Account const& account =
                    accounts.emplace_back(Account::AcctStringType::Base58Seed, std::string(seed));
                env.fund(XRP(10000), account);

                // Do not close the ledger inside the loop.  If accounts are
                // initialized at different ledgers, they will have
                // different account sequences.  That would cause the
                // accounts to have different NFTokenID sequence numbers.
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
                uint256 const& nftID =
                    nftIDs.emplace_back(token::getNextID(env, account, 0, tfTransferable));
                env(token::mint(account, 0), Txflags(tfTransferable));
                env.close();

                // Create an offer to give the NFT to buyer for free.
                offers.emplace_back(keylet::nftokenOffer(account, env.seq(account)).key);
                env(token::createOffer(account, nftID, XRP(0)),
                    token::Destination(buyer),
                    Txflags(tfSellNFToken));
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
                uint256 const offerID = keylet::nftokenOffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, XRP(100)), Txflags(tfSellNFToken));
                env.close();

                env(token::cancelOffer(buyer, {offerID}));
            }

            // Verify that all the NFTs are owned by buyer.
            json::Value buyerNFTs = [&env, &buyer]() {
                json::Value params;
                params[jss::account] = buyer.human();
                params[jss::type] = "state";
                return env.rpc("json", "account_nfts", to_string(params));
            }();

            BEAST_EXPECT(buyerNFTs[jss::result][jss::account_nfts].size() == nftIDs.size());
            for (json::Value const& ownedNFT : buyerNFTs[jss::result][jss::account_nfts])
            {
                uint256 ownedID;
                BEAST_EXPECT(ownedID.parseHex(ownedNFT[sfNFTokenID.jsonName].asString()));
                auto const foundIter = std::ranges::find(nftIDs, ownedID);

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
        static std::initializer_list<std::string_view const> const kSplitAndAddToHi{
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
        static std::initializer_list<std::string_view const> const kSplitAndAddToLo{
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
        exerciseLopsided(kSplitAndAddToHi);
        exerciseLopsided(kSplitAndAddToLo);
    }

    void
    testNFTokenDir(FeatureBitset features)
    {
        testcase("NFTokenDir");

        using namespace test::jtx;

        // When a single NFT page exceeds 32 entries, the code is inclined
        // to split that page into two equal pieces.  The new page is lower
        // than the original.  There was an off-by-one in the selection of
        // the index for the new page.  This test recreates the problem.

        // Lambda that exercises the split.
        auto exercise = [this, &features](std::initializer_list<std::string_view const> seeds) {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};

            // Eventually all of the NFTokens will be owned by buyer.
            Account const buyer{"buyer"};
            env.fund(XRP(10000), buyer);
            env.close();

            // Create accounts for all of the seeds and fund those accounts.
            std::vector<Account> accounts;
            accounts.reserve(seeds.size());
            for (std::string_view const seed : seeds)
            {
                Account const& account =
                    accounts.emplace_back(Account::AcctStringType::Base58Seed, std::string(seed));
                env.fund(XRP(10000), account);

                // Do not close the ledger inside the loop.  If accounts are
                // initialized at different ledgers, they will have
                // different account sequences.  That would cause the
                // accounts to have different NFTokenID sequence numbers.
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
                uint256 const& nftID =
                    nftIDs.emplace_back(token::getNextID(env, account, 0, tfTransferable));
                env(token::mint(account, 0), Txflags(tfTransferable));
                env.close();

                // Create an offer to give the NFT to buyer for free.
                offers.emplace_back(keylet::nftokenOffer(account, env.seq(account)).key);
                env(token::createOffer(account, nftID, XRP(0)),
                    token::Destination(buyer),
                    Txflags(tfSellNFToken));
            }
            env.close();

            // buyer accepts all of the but the last.  The last offer
            // causes the page to split.
            for (std::size_t i = 0; i < offers.size() - 1; ++i)
            {
                env(token::acceptSellOffer(buyer, offers[i]));
                env.close();
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
                uint256 const offerID = keylet::nftokenOffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, XRP(100)), Txflags(tfSellNFToken));
                env.close();

                env(token::cancelOffer(buyer, {offerID}));
            }

            // Verify that all the NFTs are owned by buyer.
            json::Value buyerNFTs = [&env, &buyer]() {
                json::Value params;
                params[jss::account] = buyer.human();
                params[jss::type] = "state";
                return env.rpc("json", "account_nfts", to_string(params));
            }();

            BEAST_EXPECT(buyerNFTs[jss::result][jss::account_nfts].size() == nftIDs.size());
            for (json::Value const& ownedNFT : buyerNFTs[jss::result][jss::account_nfts])
            {
                uint256 ownedID;
                BEAST_EXPECT(ownedID.parseHex(ownedNFT[sfNFTokenID.jsonName].asString()));
                auto const foundIter = std::ranges::find(nftIDs, ownedID);

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
        static std::initializer_list<std::string_view const> const kSeventeenHi{
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
        static std::initializer_list<std::string_view const> const kSeventeenLo{
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
        exercise(kSeventeenHi);
        exercise(kSeventeenLo);
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
        static std::initializer_list<std::string_view const> const kSeeds{
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
        accounts.reserve(kSeeds.size());
        for (std::string_view const seed : kSeeds)
        {
            Account const& account =
                accounts.emplace_back(Account::AcctStringType::Base58Seed, std::string(seed));
            env.fund(XRP(10000), account);

            // Do not close the ledger inside the loop.  If accounts are
            // initialized at different ledgers, they will have different
            // account sequences.  That would cause the accounts to have
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
            uint256 const& nftID =
                nftIDs.emplace_back(token::getNextID(env, account, 0, tfTransferable));
            env(token::mint(account, 0), Txflags(tfTransferable));
            env.close();

            // Create an offer to give the NFT to buyer for free.
            offers.emplace_back(keylet::nftokenOffer(account, env.seq(account)).key);
            env(token::createOffer(account, nftID, XRP(0)),
                token::Destination(buyer),
                Txflags(tfSellNFToken));
        }
        env.close();

        // Verify that the low 96 bits of all generated NFTs is identical.
        uint256 const expectLowBits = nftIDs.front() & nft::kPageMask;
        for (uint256 const& nftID : nftIDs)
        {
            BEAST_EXPECT(expectLowBits == (nftID & nft::kPageMask));
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
        env(token::acceptSellOffer(buyer, offerForPageOverflow), Ter(tecNO_SUITABLE_NFTOKEN_PAGE));

        // Verify that all expected NFTs are owned by buyer and findable in
        // the ledger by having buyer create sell offers for all of their NFTs.
        // Attempting to sell an offer that the ledger can't find generates
        // a non-tesSUCCESS error code.
        for (uint256 const& nftID : nftIDs)
        {
            uint256 const offerID = keylet::nftokenOffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftID, XRP(100)), Txflags(tfSellNFToken));
            env.close();

            env(token::cancelOffer(buyer, {offerID}));
        }

        // Verify that all the NFTs are owned by buyer.
        json::Value buyerNFTs = [&env, &buyer]() {
            json::Value params;
            params[jss::account] = buyer.human();
            params[jss::type] = "state";
            return env.rpc("json", "account_nfts", to_string(params));
        }();

        BEAST_EXPECT(buyerNFTs[jss::result][jss::account_nfts].size() == nftIDs.size());
        for (json::Value const& ownedNFT : buyerNFTs[jss::result][jss::account_nfts])
        {
            uint256 ownedID;
            BEAST_EXPECT(ownedID.parseHex(ownedNFT[sfNFTokenID.jsonName].asString()));
            auto const foundIter = std::ranges::find(nftIDs, ownedID);

            // Assuming we find the NFT, erase it so we know it's been found
            // and can't be found again.
            if (BEAST_EXPECT(foundIter != nftIDs.end()))
                nftIDs.erase(foundIter);
        }

        // All NFTs should now be accounted for, so nftIDs should be empty.
        BEAST_EXPECT(nftIDs.empty());

        TER const expect = tesSUCCESS;
        env(token::mint(buyer, 0), Txflags(tfTransferable), Ter(expect));
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
        // Lastly, kNone of the remaining NFTs should be acquirable by the
        // buyer.  They would cause page overflow.

        testcase("NFToken consecutive packing");

        using namespace test::jtx;

        Env env{*this, features};

        // Eventually all of the NFTokens will be owned by buyer.
        Account const buyer{"buyer"};
        env.fund(XRP(10000), buyer);
        env.close();

        // Here are 33 seeds that produce identical low 32-bits in their
        // corresponding AccountIDs.
        static std::initializer_list<std::string_view const> const kSeeds{
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
        accounts.reserve(kSeeds.size());
        for (std::string_view const seed : kSeeds)
        {
            Account const& account =
                accounts.emplace_back(Account::AcctStringType::Base58Seed, std::string(seed));
            env.fund(XRP(10000), account);

            // Do not close the ledger inside the loop.  If accounts are
            // initialized at different ledgers, they will have different
            // account sequences.  That would cause the accounts to have
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
                std::uint32_t const taxon = toUInt32(nft::cipheredTaxon(i, nft::toTaxon(0)));

                uint256 const& nftID = nftIDsByPage[i].emplace_back(
                    token::getNextID(env, account, taxon, tfTransferable));
                env(token::mint(account, taxon), Txflags(tfTransferable));
                env.close();

                // Create an offer to give the NFT to buyer for free.
                offers[i].emplace_back(keylet::nftokenOffer(account, env.seq(account)).key);
                env(token::createOffer(account, nftID, XRP(0)),
                    token::Destination(buyer),
                    Txflags(tfSellNFToken));
            }
        }
        env.close();

        // Verify that the low 96 bits of all generated NFTs of the same
        // sequence is identical.
        for (auto const& vec : nftIDsByPage)
        {
            uint256 const expectLowBits = vec.front() & nft::kPageMask;
            for (uint256 const& nftID : vec)
            {
                BEAST_EXPECT(expectLowBits == (nftID & nft::kPageMask));
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
            BEAST_EXPECT(nftIDsByPage[i].size() == kSeeds.size() - 1);

            overflowOffers.push_back(offers[i].back());
            offers[i].pop_back();
            BEAST_EXPECT(offers[i].size() == kSeeds.size() - 1);
        }

        // buyer accepts all of the offers that won't cause an overflow.
        // Fill the center and outsides first to exercise different boundary
        // cases.
        for (int const i : std::initializer_list<int>{3, 6, 0, 1, 2, 5, 4})
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
            env(token::acceptSellOffer(buyer, offer), Ter(tecNO_SUITABLE_NFTOKEN_PAGE));
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
                env(token::createOffer(buyer, nftID, XRP(100)), Txflags(tfSellNFToken));
                env.close();
            }
        }

        // See what the account_objects command does with "nft_offer".
        {
            json::Value ownedNftOffers(json::ValueType::Array);
            std::string marker;
            do
            {
                json::Value buyerOffers = [&env, &buyer, &marker]() {
                    json::Value params;
                    params[jss::account] = buyer.human();
                    params[jss::type] = jss::nft_offer;

                    if (!marker.empty())
                        params[jss::marker] = marker;
                    return env.rpc("json", "account_objects", to_string(params));
                }();

                marker.clear();
                if (buyerOffers.isMember(jss::result))
                {
                    json::Value& result = buyerOffers[jss::result];

                    if (result.isMember(jss::marker))
                        marker = result[jss::marker].asString();

                    if (result.isMember(jss::account_objects))
                    {
                        json::Value& someOffers = result[jss::account_objects];
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
            json::Value remainingOffers = [&env, &buyer]() {
                json::Value params;
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
        json::Value ownedNFTs(json::ValueType::Array);
        std::string marker;
        do
        {
            json::Value buyerNFTs = [&env, &buyer, &marker]() {
                json::Value params;
                params[jss::account] = buyer.human();
                params[jss::type] = "state";

                if (!marker.empty())
                    params[jss::marker] = marker;
                return env.rpc("json", "account_nfts", to_string(params));
            }();

            marker.clear();
            if (buyerNFTs.isMember(jss::result))
            {
                json::Value& result = buyerNFTs[jss::result];

                if (result.isMember(jss::marker))
                    marker = result[jss::marker].asString();

                if (result.isMember(jss::account_nfts))
                {
                    json::Value& someNFTs = result[jss::account_nfts];
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

        for (json::Value const& ownedNFT : ownedNFTs)
        {
            if (ownedNFT.isMember(sfNFTokenID.jsonName))
            {
                uint256 ownedID;
                BEAST_EXPECT(ownedID.parseHex(ownedNFT[sfNFTokenID.jsonName].asString()));
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
        testNFTokenDir(features);
        testTooManyEquivalent(features);
        testConsecutivePacking(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};

        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(NFTokenDir, app, xrpl, 1);

}  // namespace xrpl

// Seed that produces an account with the low-32 bits == 0xFFFFFFFF in
// case it is needed for future testing:
//
//   sp6JS7f14BuwFY8MwFe95Vpi9Znjs
//
