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

#include <xrpl/basics/random.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

#include <initializer_list>

namespace ripple {

class NFTokenBaseUtil_test : public beast::unit_test::suite
{
    FeatureBitset const disallowIncoming{featureDisallowIncoming};

    // Helper function that returns the number of NFTs minted by an issuer.
    static std::uint32_t
    mintedCount(test::jtx::Env const& env, test::jtx::Account const& issuer)
    {
        std::uint32_t ret{0};
        if (auto const sleIssuer = env.le(issuer))
            ret = sleIssuer->at(~sfMintedNFTokens).value_or(0);
        return ret;
    }

    // Helper function that returns the number of an issuer's burned NFTs.
    static std::uint32_t
    burnedCount(test::jtx::Env const& env, test::jtx::Account const& issuer)
    {
        std::uint32_t ret{0};
        if (auto const sleIssuer = env.le(issuer))
            ret = sleIssuer->at(~sfBurnedNFTokens).value_or(0);
        return ret;
    }

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

    // Helper function that returns the number of tickets held by an account.
    static std::uint32_t
    ticketCount(test::jtx::Env const& env, test::jtx::Account const& acct)
    {
        std::uint32_t ret{0};
        if (auto const sleAcct = env.le(acct))
            ret = sleAcct->at(~sfTicketCount).value_or(0);
        return ret;
    }

    // Helper function returns the close time of the parent ledger.
    std::uint32_t
    lastClose(test::jtx::Env& env)
    {
        return env.current()->info().parentCloseTime.time_since_epoch().count();
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("Enabled");

        using namespace test::jtx;
        {
            // If the NFT amendment is not enabled, you should not be able
            // to create or burn NFTs.
            Env env{
                *this,
                features - featureNonFungibleTokensV1 -
                    featureNonFungibleTokensV1_1};
            Account const& master = env.master;

            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 0);
            BEAST_EXPECT(burnedCount(env, master) == 0);

            uint256 const nftId{token::getNextID(env, master, 0u)};
            env(token::mint(master, 0u), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 0);
            BEAST_EXPECT(burnedCount(env, master) == 0);

            env(token::burn(master, nftId), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 0);
            BEAST_EXPECT(burnedCount(env, master) == 0);

            uint256 const offerIndex =
                keylet::nftoffer(master, env.seq(master)).key;
            env(token::createOffer(master, nftId, XRP(10)), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 0);
            BEAST_EXPECT(burnedCount(env, master) == 0);

            env(token::cancelOffer(master, {offerIndex}), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 0);
            BEAST_EXPECT(burnedCount(env, master) == 0);

            env(token::acceptBuyOffer(master, offerIndex), ter(temDISABLED));
            env.close();
            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 0);
            BEAST_EXPECT(burnedCount(env, master) == 0);
        }
        {
            // If the NFT amendment is enabled all NFT-related
            // facilities should be available.
            Env env{*this, features};
            Account const& master = env.master;

            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 0);
            BEAST_EXPECT(burnedCount(env, master) == 0);

            uint256 const nftId0{token::getNextID(env, env.master, 0u)};
            env(token::mint(env.master, 0u));
            env.close();
            BEAST_EXPECT(ownerCount(env, master) == 1);
            BEAST_EXPECT(mintedCount(env, master) == 1);
            BEAST_EXPECT(burnedCount(env, master) == 0);

            env(token::burn(env.master, nftId0));
            env.close();
            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 1);
            BEAST_EXPECT(burnedCount(env, master) == 1);

            uint256 const nftId1{
                token::getNextID(env, env.master, 0u, tfTransferable)};
            env(token::mint(env.master, 0u), txflags(tfTransferable));
            env.close();
            BEAST_EXPECT(ownerCount(env, master) == 1);
            BEAST_EXPECT(mintedCount(env, master) == 2);
            BEAST_EXPECT(burnedCount(env, master) == 1);

            Account const alice{"alice"};
            env.fund(XRP(10000), alice);
            env.close();
            uint256 const aliceOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId1, XRP(1000)),
                token::owner(master));
            env.close();

            BEAST_EXPECT(ownerCount(env, master) == 1);
            BEAST_EXPECT(mintedCount(env, master) == 2);
            BEAST_EXPECT(burnedCount(env, master) == 1);

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(mintedCount(env, alice) == 0);
            BEAST_EXPECT(burnedCount(env, alice) == 0);

            env(token::acceptBuyOffer(master, aliceOfferIndex));
            env.close();

            BEAST_EXPECT(ownerCount(env, master) == 0);
            BEAST_EXPECT(mintedCount(env, master) == 2);
            BEAST_EXPECT(burnedCount(env, master) == 1);

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(mintedCount(env, alice) == 0);
            BEAST_EXPECT(burnedCount(env, alice) == 0);
        }
    }

    void
    testMintReserve(FeatureBitset features)
    {
        // Verify that the reserve behaves as expected for minting.
        testcase("Mint reserve");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const minter{"minter"};

        // Fund alice and minter enough to exist, but not enough to meet
        // the reserve for creating their first NFT.
        auto const acctReserve = env.current()->fees().accountReserve(0);
        auto const incReserve = env.current()->fees().increment;
        auto const baseFee = env.current()->fees().base;

        env.fund(acctReserve, alice, minter);
        env.close();

        BEAST_EXPECT(env.balance(alice) == acctReserve);
        BEAST_EXPECT(env.balance(minter) == acctReserve);
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, minter) == 0);

        // alice does not have enough XRP to cover the reserve for an NFT
        // page.
        env(token::mint(alice, 0u), ter(tecINSUFFICIENT_RESERVE));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(mintedCount(env, alice) == 0);
        BEAST_EXPECT(burnedCount(env, alice) == 0);

        // Pay alice almost enough to make the reserve for an NFT page.
        env(pay(env.master, alice, incReserve + drops(baseFee - 1)));
        env.close();

        // A lambda that checks alice's ownerCount, mintedCount, and
        // burnedCount all in one fell swoop.
        auto checkAliceOwnerMintedBurned = [&env, this, &alice](
                                               std::uint32_t owners,
                                               std::uint32_t minted,
                                               std::uint32_t burned,
                                               int line) {
            auto oneCheck =
                [line, this](
                    char const* type, std::uint32_t found, std::uint32_t exp) {
                    if (found == exp)
                        pass();
                    else
                    {
                        std::stringstream ss;
                        ss << "Wrong " << type << " count.  Found: " << found
                           << "; Expected: " << exp;
                        fail(ss.str(), __FILE__, line);
                    }
                };
            oneCheck("owner", ownerCount(env, alice), owners);
            oneCheck("minted", mintedCount(env, alice), minted);
            oneCheck("burned", burnedCount(env, alice), burned);
        };

        // alice still does not have enough XRP for the reserve of an NFT
        // page.
        env(token::mint(alice, 0u), ter(tecINSUFFICIENT_RESERVE));
        env.close();

        checkAliceOwnerMintedBurned(0, 0, 0, __LINE__);

        // Pay alice enough to make the reserve for an NFT page.
        env(pay(env.master, alice, drops(baseFee + 1)));
        env.close();

        // Now alice can mint an NFT.
        env(token::mint(alice));
        env.close();

        checkAliceOwnerMintedBurned(1, 1, 0, __LINE__);

        // Alice should be able to mint an additional 31 NFTs without
        // any additional reserve requirements.
        for (int i = 1; i < 32; ++i)
        {
            env(token::mint(alice));
            checkAliceOwnerMintedBurned(1, i + 1, 0, __LINE__);
        }

        // That NFT page is full.  Creating an additional NFT page requires
        // additional reserve.
        env(token::mint(alice), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        checkAliceOwnerMintedBurned(1, 32, 0, __LINE__);

        // Pay alice almost enough to make the reserve for an NFT page.
        env(pay(env.master, alice, incReserve + drops(baseFee * 33 - 1)));
        env.close();

        // alice still does not have enough XRP for the reserve of an NFT
        // page.
        env(token::mint(alice), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        checkAliceOwnerMintedBurned(1, 32, 0, __LINE__);

        // Pay alice enough to make the reserve for an NFT page.
        env(pay(env.master, alice, drops(baseFee + 1)));
        env.close();

        // Now alice can mint an NFT.
        env(token::mint(alice));
        env.close();
        checkAliceOwnerMintedBurned(2, 33, 0, __LINE__);

        // alice burns the NFTs she created: check that pages consolidate
        std::uint32_t seq = 0;

        while (seq < 33)
        {
            env(token::burn(alice, token::getID(env, alice, 0, seq++)));
            env.close();
            checkAliceOwnerMintedBurned((33 - seq) ? 1 : 0, 33, seq, __LINE__);
        }

        // alice burns a non-existent NFT.
        env(token::burn(alice, token::getID(env, alice, 197, 5)),
            ter(tecNO_ENTRY));
        env.close();
        checkAliceOwnerMintedBurned(0, 33, 33, __LINE__);

        // That was fun!  Now let's see what happens when we let someone
        // else mint NFTs on alice's behalf.  alice gives permission to
        // minter.
        env(token::setMinter(alice, minter));
        env.close();
        BEAST_EXPECT(
            env.le(alice)->getAccountID(sfNFTokenMinter) == minter.id());

        // A lambda that checks minter's and alice's ownerCount,
        // mintedCount, and burnedCount all in one fell swoop.
        auto checkMintersOwnerMintedBurned = [&env, this, &alice, &minter](
                                                 std::uint32_t aliceOwners,
                                                 std::uint32_t aliceMinted,
                                                 std::uint32_t aliceBurned,
                                                 std::uint32_t minterOwners,
                                                 std::uint32_t minterMinted,
                                                 std::uint32_t minterBurned,
                                                 int line) {
            auto oneCheck = [this](
                                char const* type,
                                std::uint32_t found,
                                std::uint32_t exp,
                                int line) {
                if (found == exp)
                    pass();
                else
                {
                    std::stringstream ss;
                    ss << "Wrong " << type << " count.  Found: " << found
                       << "; Expected: " << exp;
                    fail(ss.str(), __FILE__, line);
                }
            };
            oneCheck("alice owner", ownerCount(env, alice), aliceOwners, line);
            oneCheck(
                "alice minted", mintedCount(env, alice), aliceMinted, line);
            oneCheck(
                "alice burned", burnedCount(env, alice), aliceBurned, line);
            oneCheck(
                "minter owner", ownerCount(env, minter), minterOwners, line);
            oneCheck(
                "minter minted", mintedCount(env, minter), minterMinted, line);
            oneCheck(
                "minter burned", burnedCount(env, minter), minterBurned, line);
        };

        std::uint32_t nftSeq = 33;

        // Pay minter almost enough to make the reserve for an NFT page.
        env(pay(env.master, minter, incReserve - drops(1)));
        env.close();
        checkMintersOwnerMintedBurned(0, 33, nftSeq, 0, 0, 0, __LINE__);

        // minter still does not have enough XRP for the reserve of an NFT
        // page. Just for grins (and code coverage), minter mints NFTs that
        // include a URI.
        env(token::mint(minter),
            token::issuer(alice),
            token::uri("uri"),
            ter(tecINSUFFICIENT_RESERVE));
        env.close();
        checkMintersOwnerMintedBurned(0, 33, nftSeq, 0, 0, 0, __LINE__);

        // Pay minter enough to make the reserve for an NFT page.
        env(pay(env.master, minter, drops(baseFee + 1)));
        env.close();

        // Now minter can mint an NFT for alice.
        env(token::mint(minter), token::issuer(alice), token::uri("uri"));
        env.close();
        checkMintersOwnerMintedBurned(0, 34, nftSeq, 1, 0, 0, __LINE__);

        // Minter should be able to mint an additional 31 NFTs for alice
        // without any additional reserve requirements.
        for (int i = 1; i < 32; ++i)
        {
            env(token::mint(minter), token::issuer(alice), token::uri("uri"));
            checkMintersOwnerMintedBurned(0, i + 34, nftSeq, 1, 0, 0, __LINE__);
        }

        // Pay minter almost enough for the reserve of an additional NFT
        // page.
        env(pay(env.master, minter, incReserve + drops(baseFee * 32 - 1)));
        env.close();

        // That NFT page is full.  Creating an additional NFT page requires
        // additional reserve.
        env(token::mint(minter),
            token::issuer(alice),
            token::uri("uri"),
            ter(tecINSUFFICIENT_RESERVE));
        env.close();
        checkMintersOwnerMintedBurned(0, 65, nftSeq, 1, 0, 0, __LINE__);

        // Pay minter enough for the reserve of an additional NFT page.
        env(pay(env.master, minter, drops(baseFee + 1)));
        env.close();

        // Now minter can mint an NFT.
        env(token::mint(minter), token::issuer(alice), token::uri("uri"));
        env.close();
        checkMintersOwnerMintedBurned(0, 66, nftSeq, 2, 0, 0, __LINE__);

        // minter burns the NFTs she created.
        while (nftSeq < 65)
        {
            env(token::burn(minter, token::getID(env, alice, 0, nftSeq++)));
            env.close();
            checkMintersOwnerMintedBurned(
                0, 66, nftSeq, (65 - seq) ? 1 : 0, 0, 0, __LINE__);
        }

        // minter has one more NFT to burn.  Should take her owner count to
        // 0.
        env(token::burn(minter, token::getID(env, alice, 0, nftSeq++)));
        env.close();
        checkMintersOwnerMintedBurned(0, 66, nftSeq, 0, 0, 0, __LINE__);

        // minter burns a non-existent NFT.
        env(token::burn(minter, token::getID(env, alice, 2009, 3)),
            ter(tecNO_ENTRY));
        env.close();
        checkMintersOwnerMintedBurned(0, 66, nftSeq, 0, 0, 0, __LINE__);
    }

    void
    testMintMaxTokens(FeatureBitset features)
    {
        // Make sure that an account cannot cause the sfMintedNFTokens
        // field to wrap by minting more than 0xFFFF'FFFF tokens.
        testcase("Mint max tokens");

        using namespace test::jtx;

        Account const alice{"alice"};
        Env env{*this, features};
        env.fund(XRP(1000), alice);
        env.close();

        // We're going to hack the ledger in order to avoid generating
        // 4 billion or so NFTs.  Because we're hacking the ledger we
        // need alice's account to have non-zero sfMintedNFTokens and
        // sfBurnedNFTokens fields.  This prevents an exception when the
        // AccountRoot template is applied.
        {
            uint256 const nftId0{token::getNextID(env, alice, 0u)};
            env(token::mint(alice, 0u));
            env.close();

            env(token::burn(alice, nftId0));
            env.close();
        }

        // Note that we're bypassing almost all of the ledger's safety
        // checks with this modify() call.  If you call close() between
        // here and the end of the test all the effort will be lost.
        env.app().openLedger().modify(
            [&alice, &env](OpenView& view, beast::Journal j) {
                // Get the account root we want to hijack.
                auto const sle = view.read(keylet::account(alice.id()));
                if (!sle)
                    return false;  // This would be really surprising!

                // Just for sanity's sake we'll check that the current value
                // of sfMintedNFTokens matches what we expect.
                auto replacement = std::make_shared<SLE>(*sle, sle->key());
                if (replacement->getFieldU32(sfMintedNFTokens) != 1)
                    return false;  // Unexpected test conditions.

                if (env.current()->rules().enabled(fixNFTokenRemint))
                {
                    // If fixNFTokenRemint is enabled, sequence number is
                    // generated by sfFirstNFTokenSequence + sfMintedNFTokens.
                    // We can replace the two fields with any numbers as long as
                    // they add up to the largest valid number. In our case,
                    // sfFirstNFTokenSequence is set to the largest valid
                    // number, and sfMintedNFTokens is set to zero.
                    (*replacement)[sfFirstNFTokenSequence] = 0xFFFF'FFFE;
                    (*replacement)[sfMintedNFTokens] = 0x0000'0000;
                }
                else
                {
                    // Now replace sfMintedNFTokens with the largest valid
                    // value.
                    (*replacement)[sfMintedNFTokens] = 0xFFFF'FFFE;
                }
                view.rawReplace(replacement);
                return true;
            });

        // See whether alice is at the boundary that causes an error.
        env(token::mint(alice, 0u), ter(tesSUCCESS));
        env(token::mint(alice, 0u), ter(tecMAX_SEQUENCE_REACHED));
    }

    void
    testMintInvalid(FeatureBitset features)
    {
        // Explore many of the invalid ways to mint an NFT.
        testcase("Mint invalid");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const minter{"minter"};

        // Fund alice and minter enough to exist, but not enough to meet
        // the reserve for creating their first NFT.  Account reserve for unit
        // tests is 200 XRP, not 20.
        env.fund(XRP(200), alice, minter);
        env.close();

        env(token::mint(alice, 0u), ter(tecINSUFFICIENT_RESERVE));
        env.close();

        // Fund alice enough to start minting NFTs.
        env(pay(env.master, alice, XRP(1000)));
        env.close();

        //----------------------------------------------------------------------
        // preflight

        // Set a negative fee.
        env(token::mint(alice, 0u),
            fee(STAmount(10ull, true)),
            ter(temBAD_FEE));

        // Set an invalid flag.
        env(token::mint(alice, 0u), txflags(0x00008000), ter(temINVALID_FLAG));

        // Can't set a transfer fee if the NFT does not have the tfTRANSFERABLE
        // flag set.
        env(token::mint(alice, 0u),
            token::xferFee(maxTransferFee),
            ter(temMALFORMED));

        // Set a bad transfer fee.
        env(token::mint(alice, 0u),
            token::xferFee(maxTransferFee + 1),
            txflags(tfTransferable),
            ter(temBAD_NFTOKEN_TRANSFER_FEE));

        // Account can't also be issuer.
        env(token::mint(alice, 0u), token::issuer(alice), ter(temMALFORMED));

        // Invalid URI: zero length.
        env(token::mint(alice, 0u), token::uri(""), ter(temMALFORMED));

        // Invalid URI: too long.
        env(token::mint(alice, 0u),
            token::uri(std::string(maxTokenURILength + 1, 'q')),
            ter(temMALFORMED));

        //----------------------------------------------------------------------
        // preclaim

        // Non-existent issuer.
        env(token::mint(alice, 0u),
            token::issuer(Account("demon")),
            ter(tecNO_ISSUER));

        //----------------------------------------------------------------------
        // doApply

        // Existent issuer, but not given minting permission
        env(token::mint(minter, 0u),
            token::issuer(alice),
            ter(tecNO_PERMISSION));
    }

    void
    testBurnInvalid(FeatureBitset features)
    {
        // Explore many of the invalid ways to burn an NFT.
        testcase("Burn invalid");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const buyer{"buyer"};
        Account const minter{"minter"};
        Account const gw("gw");
        IOU const gwAUD(gw["AUD"]);

        // Fund alice and minter enough to exist and create an NFT, but not
        // enough to meet the reserve for creating their first NFTOffer.
        // Account reserve for unit tests is 200 XRP, not 20.
        env.fund(XRP(250), alice, buyer, minter, gw);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        uint256 const nftAlice0ID =
            token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0u), txflags(tfTransferable));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        //----------------------------------------------------------------------
        // preflight

        // Set a negative fee.
        env(token::burn(alice, nftAlice0ID),
            fee(STAmount(10ull, true)),
            ter(temBAD_FEE));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // Set an invalid flag.
        env(token::burn(alice, nftAlice0ID),
            txflags(0x00008000),
            ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        //----------------------------------------------------------------------
        // preclaim

        // Try to burn a token that doesn't exist.
        env(token::burn(alice, token::getID(env, alice, 0, 1)),
            ter(tecNO_ENTRY));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // Can't burn a token with many buy or sell offers.  But that is
        // verified in testManyNftOffers().

        //----------------------------------------------------------------------
        // doApply
    }

    void
    testCreateOfferInvalid(FeatureBitset features)
    {
        testcase("Invalid NFT offer create");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const buyer{"buyer"};
        Account const gw("gw");
        IOU const gwAUD(gw["AUD"]);

        // Fund alice enough to exist and create an NFT, but not
        // enough to meet the reserve for creating their first NFTOffer.
        // Account reserve for unit tests is 200 XRP, not 20.
        env.fund(XRP(250), alice, buyer, gw);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        uint256 const nftAlice0ID =
            token::getNextID(env, alice, 0, tfTransferable, 10);
        env(token::mint(alice, 0u),
            txflags(tfTransferable),
            token::xferFee(10));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        uint256 const nftXrpOnlyID =
            token::getNextID(env, alice, 0, tfOnlyXRP | tfTransferable);
        env(token::mint(alice, 0), txflags(tfOnlyXRP | tfTransferable));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        uint256 nftNoXferID = token::getNextID(env, alice, 0);
        env(token::mint(alice, 0));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        //----------------------------------------------------------------------
        // preflight

        // buyer burns a fee, so they no longer have enough XRP to cover the
        // reserve for a token offer.
        env(noop(buyer));
        env.close();

        // buyer tries to create an NFTokenOffer, but doesn't have the reserve.
        env(token::createOffer(buyer, nftAlice0ID, XRP(1000)),
            token::owner(alice),
            ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // Set a negative fee.
        env(token::createOffer(buyer, nftAlice0ID, XRP(1000)),
            fee(STAmount(10ull, true)),
            ter(temBAD_FEE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // Set an invalid flag.
        env(token::createOffer(buyer, nftAlice0ID, XRP(1000)),
            txflags(0x00008000),
            ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // Set an invalid amount.
        env(token::createOffer(buyer, nftXrpOnlyID, buyer["USD"](1)),
            ter(temBAD_AMOUNT));
        env(token::createOffer(buyer, nftAlice0ID, buyer["USD"](0)),
            ter(temBAD_AMOUNT));
        env(token::createOffer(buyer, nftXrpOnlyID, drops(0)),
            ter(temBAD_AMOUNT));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // Set a bad expiration.
        env(token::createOffer(buyer, nftAlice0ID, buyer["USD"](1)),
            token::expiration(0),
            ter(temBAD_EXPIRATION));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // Invalid Owner field and tfSellToken flag relationships.
        // A buy offer must specify the owner.
        env(token::createOffer(buyer, nftXrpOnlyID, XRP(1000)),
            ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // A sell offer must not specify the owner; the owner is implicit.
        env(token::createOffer(alice, nftXrpOnlyID, XRP(1000)),
            token::owner(alice),
            txflags(tfSellNFToken),
            ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // An owner may not offer to buy their own token.
        env(token::createOffer(alice, nftXrpOnlyID, XRP(1000)),
            token::owner(alice),
            ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // The destination may not be the account submitting the transaction.
        env(token::createOffer(alice, nftXrpOnlyID, XRP(1000)),
            token::destination(alice),
            txflags(tfSellNFToken),
            ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // The destination must be an account already established in the ledger.
        env(token::createOffer(alice, nftXrpOnlyID, XRP(1000)),
            token::destination(Account("demon")),
            txflags(tfSellNFToken),
            ter(tecNO_DST));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        //----------------------------------------------------------------------
        // preclaim

        // The new NFTokenOffer may not have passed its expiration time.
        env(token::createOffer(buyer, nftXrpOnlyID, XRP(1000)),
            token::owner(alice),
            token::expiration(lastClose(env)),
            ter(tecEXPIRED));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // The nftID must be present in the ledger.
        env(token::createOffer(
                buyer, token::getID(env, alice, 0, 1), XRP(1000)),
            token::owner(alice),
            ter(tecNO_ENTRY));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // The nftID must be present in the ledger of a sell offer too.
        env(token::createOffer(
                alice, token::getID(env, alice, 0, 1), XRP(1000)),
            txflags(tfSellNFToken),
            ter(tecNO_ENTRY));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // buyer must have the funds to pay for their offer.
        env(token::createOffer(buyer, nftAlice0ID, gwAUD(1000)),
            token::owner(alice),
            ter(tecNO_LINE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        env(trust(buyer, gwAUD(1000)));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);
        env.close();

        // Issuer (alice) must have a trust line for the offered funds.
        env(token::createOffer(buyer, nftAlice0ID, gwAUD(1000)),
            token::owner(alice),
            ter(tecNO_LINE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Give alice the needed trust line, but freeze it.
        env(trust(gw, alice["AUD"](999), tfSetFreeze));
        env.close();

        // Issuer (alice) must have a trust line for the offered funds and
        // the trust line may not be frozen.
        env(token::createOffer(buyer, nftAlice0ID, gwAUD(1000)),
            token::owner(alice),
            ter(tecFROZEN));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Unfreeze alice's trustline.
        env(trust(gw, alice["AUD"](999), tfClearFreeze));
        env.close();

        // Can't transfer the NFT if the transferable flag is not set.
        env(token::createOffer(buyer, nftNoXferID, gwAUD(1000)),
            token::owner(alice),
            ter(tefNFTOKEN_IS_NOT_TRANSFERABLE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Give buyer the needed trust line, but freeze it.
        env(trust(gw, buyer["AUD"](999), tfSetFreeze));
        env.close();

        env(token::createOffer(buyer, nftAlice0ID, gwAUD(1000)),
            token::owner(alice),
            ter(tecFROZEN));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Unfreeze buyer's trust line, but buyer has no actual gwAUD.
        // to cover the offer.
        env(trust(gw, buyer["AUD"](999), tfClearFreeze));
        env(trust(buyer, gwAUD(1000)));
        env.close();

        env(token::createOffer(buyer, nftAlice0ID, gwAUD(1000)),
            token::owner(alice),
            ter(tecUNFUNDED_OFFER));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);  // the trust line.

        //----------------------------------------------------------------------
        // doApply

        // Give buyer almost enough AUD to cover the offer...
        env(pay(gw, buyer, gwAUD(999)));
        env.close();

        // However buyer doesn't have enough XRP to cover the reserve for
        // an NFT offer.
        env(token::createOffer(buyer, nftAlice0ID, gwAUD(1000)),
            token::owner(alice),
            ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Give buyer almost enough XRP to cover the reserve.
        auto const baseFee = env.current()->fees().base;
        env(pay(env.master, buyer, XRP(50) + drops(baseFee * 12 - 1)));
        env.close();

        env(token::createOffer(buyer, nftAlice0ID, gwAUD(1000)),
            token::owner(alice),
            ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Give buyer just enough XRP to cover the reserve for the offer.
        env(pay(env.master, buyer, drops(baseFee + 1)));
        env.close();

        // We don't care whether the offer is fully funded until the offer is
        // accepted.  Success at last!
        env(token::createOffer(buyer, nftAlice0ID, gwAUD(1000)),
            token::owner(alice),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 2);
    }

    void
    testCancelOfferInvalid(FeatureBitset features)
    {
        testcase("Invalid NFT offer cancel");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const buyer{"buyer"};
        Account const gw("gw");
        IOU const gwAUD(gw["AUD"]);

        env.fund(XRP(1000), alice, buyer, gw);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        uint256 const nftAlice0ID =
            token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0u), txflags(tfTransferable));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // This is the offer we'll try to cancel.
        uint256 const buyerOfferIndex =
            keylet::nftoffer(buyer, env.seq(buyer)).key;
        env(token::createOffer(buyer, nftAlice0ID, XRP(1)),
            token::owner(alice),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        //----------------------------------------------------------------------
        // preflight

        // Set a negative fee.
        env(token::cancelOffer(buyer, {buyerOfferIndex}),
            fee(STAmount(10ull, true)),
            ter(temBAD_FEE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Set an invalid flag.
        env(token::cancelOffer(buyer, {buyerOfferIndex}),
            txflags(0x00008000),
            ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Empty list of tokens to delete.
        {
            Json::Value jv = token::cancelOffer(buyer);
            jv[sfNFTokenOffers.jsonName] = Json::arrayValue;
            env(jv, ter(temMALFORMED));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }

        // List of tokens to delete is too long.
        {
            std::vector<uint256> offers(
                maxTokenOfferCancelCount + 1, buyerOfferIndex);

            env(token::cancelOffer(buyer, offers), ter(temMALFORMED));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }

        // Duplicate entries are not allowed in the list of offers to cancel.
        env(token::cancelOffer(buyer, {buyerOfferIndex, buyerOfferIndex}),
            ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // Provide neither offers to cancel nor a root index.
        env(token::cancelOffer(buyer), ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        //----------------------------------------------------------------------
        // preclaim

        // Make a non-root directory that we can pass as a root index.
        env(pay(env.master, gw, XRP(5000)));
        env.close();
        for (std::uint32_t i = 1; i < 34; ++i)
        {
            env(offer(gw, XRP(i), gwAUD(1)));
            env.close();
        }

        {
            // gw attempts to cancel a Check as through it is an NFTokenOffer.
            auto const gwCheckId = keylet::check(gw, env.seq(gw)).key;
            env(check::create(gw, env.master, XRP(300)));
            env.close();

            env(token::cancelOffer(gw, {gwCheckId}), ter(tecNO_PERMISSION));
            env.close();

            // Cancel the check so it doesn't mess up later tests.
            env(check::cancel(gw, gwCheckId));
            env.close();
        }

        // gw attempts to cancel an offer they don't have permission to cancel.
        env(token::cancelOffer(gw, {buyerOfferIndex}), ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        //----------------------------------------------------------------------
        // doApply
        //
        // The tefBAD_LEDGER conditions are too hard to test.
        // But let's see a successful offer cancel.
        env(token::cancelOffer(buyer, {buyerOfferIndex}));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);
    }

    void
    testAcceptOfferInvalid(FeatureBitset features)
    {
        testcase("Invalid NFT offer accept");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const buyer{"buyer"};
        Account const gw("gw");
        IOU const gwAUD(gw["AUD"]);

        env.fund(XRP(1000), alice, buyer, gw);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        uint256 const nftAlice0ID =
            token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0u), txflags(tfTransferable));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        uint256 const nftXrpOnlyID =
            token::getNextID(env, alice, 0, tfOnlyXRP | tfTransferable);
        env(token::mint(alice, 0), txflags(tfOnlyXRP | tfTransferable));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        uint256 nftNoXferID = token::getNextID(env, alice, 0);
        env(token::mint(alice, 0));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // alice creates sell offers for her nfts.
        uint256 const plainOfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;
        env(token::createOffer(alice, nftAlice0ID, XRP(10)),
            txflags(tfSellNFToken));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        uint256 const audOfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;
        env(token::createOffer(alice, nftAlice0ID, gwAUD(30)),
            txflags(tfSellNFToken));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 3);

        uint256 const xrpOnlyOfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;
        env(token::createOffer(alice, nftXrpOnlyID, XRP(20)),
            txflags(tfSellNFToken));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 4);

        uint256 const noXferOfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;
        env(token::createOffer(alice, nftNoXferID, XRP(30)),
            txflags(tfSellNFToken));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 5);

        // alice creates a sell offer that will expire soon.
        uint256 const aliceExpOfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;
        env(token::createOffer(alice, nftNoXferID, XRP(40)),
            txflags(tfSellNFToken),
            token::expiration(lastClose(env) + 5));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 6);

        //----------------------------------------------------------------------
        // preflight

        // Set a negative fee.
        env(token::acceptSellOffer(buyer, noXferOfferIndex),
            fee(STAmount(10ull, true)),
            ter(temBAD_FEE));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // Set an invalid flag.
        env(token::acceptSellOffer(buyer, noXferOfferIndex),
            txflags(0x00008000),
            ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // Supply nether an sfNFTokenBuyOffer nor an sfNFTokenSellOffer field.
        {
            Json::Value jv = token::acceptSellOffer(buyer, noXferOfferIndex);
            jv.removeMember(sfNFTokenSellOffer.jsonName);
            env(jv, ter(temMALFORMED));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }

        // A buy offer may not contain a sfNFTokenBrokerFee field.
        {
            Json::Value jv = token::acceptBuyOffer(buyer, noXferOfferIndex);
            jv[sfNFTokenBrokerFee.jsonName] =
                STAmount(500000).getJson(JsonOptions::none);
            env(jv, ter(temMALFORMED));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }

        // A sell offer may not contain a sfNFTokenBrokerFee field.
        {
            Json::Value jv = token::acceptSellOffer(buyer, noXferOfferIndex);
            jv[sfNFTokenBrokerFee.jsonName] =
                STAmount(500000).getJson(JsonOptions::none);
            env(jv, ter(temMALFORMED));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }

        // A brokered offer may not contain a negative or zero brokerFee.
        env(token::brokerOffers(buyer, noXferOfferIndex, xrpOnlyOfferIndex),
            token::brokerFee(gwAUD(0)),
            ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        //----------------------------------------------------------------------
        // preclaim

        // The buy offer must be non-zero.
        env(token::acceptBuyOffer(buyer, beast::zero),
            ter(tecOBJECT_NOT_FOUND));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // The buy offer must be present in the ledger.
        uint256 const missingOfferIndex = keylet::nftoffer(alice, 1).key;
        env(token::acceptBuyOffer(buyer, missingOfferIndex),
            ter(tecOBJECT_NOT_FOUND));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // The buy offer must not have expired.
        env(token::acceptBuyOffer(buyer, aliceExpOfferIndex), ter(tecEXPIRED));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // The sell offer must be non-zero.
        env(token::acceptSellOffer(buyer, beast::zero),
            ter(tecOBJECT_NOT_FOUND));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // The sell offer must be present in the ledger.
        env(token::acceptSellOffer(buyer, missingOfferIndex),
            ter(tecOBJECT_NOT_FOUND));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        // The sell offer must not have expired.
        env(token::acceptSellOffer(buyer, aliceExpOfferIndex), ter(tecEXPIRED));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 0);

        //----------------------------------------------------------------------
        // preclaim brokered

        // alice and buyer need trustlines before buyer can to create an
        // offer for gwAUD.
        env(trust(alice, gwAUD(1000)));
        env(trust(buyer, gwAUD(1000)));
        env.close();
        env(pay(gw, buyer, gwAUD(30)));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 7);
        BEAST_EXPECT(ownerCount(env, buyer) == 1);

        // We're about to exercise offer brokering, so we need
        // corresponding buy and sell offers.
        {
            // buyer creates a buy offer for one of alice's nfts.
            uint256 const buyerOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftAlice0ID, gwAUD(29)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // gw attempts to broker offers that are not for the same token.
            env(token::brokerOffers(gw, buyerOfferIndex, xrpOnlyOfferIndex),
                ter(tecNFTOKEN_BUY_SELL_MISMATCH));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // gw attempts to broker offers that are not for the same currency.
            env(token::brokerOffers(gw, buyerOfferIndex, plainOfferIndex),
                ter(tecNFTOKEN_BUY_SELL_MISMATCH));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // In a brokered offer, the buyer must offer greater than or
            // equal to the selling price.
            env(token::brokerOffers(gw, buyerOfferIndex, audOfferIndex),
                ter(tecINSUFFICIENT_PAYMENT));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Remove buyer's offer.
            env(token::cancelOffer(buyer, {buyerOfferIndex}));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }
        {
            // buyer creates a buy offer for one of alice's nfts.
            uint256 const buyerOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftAlice0ID, gwAUD(31)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Broker sets their fee in a denomination other than the one
            // used by the offers
            env(token::brokerOffers(gw, buyerOfferIndex, audOfferIndex),
                token::brokerFee(XRP(40)),
                ter(tecNFTOKEN_BUY_SELL_MISMATCH));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Broker fee way too big.
            env(token::brokerOffers(gw, buyerOfferIndex, audOfferIndex),
                token::brokerFee(gwAUD(31)),
                ter(tecINSUFFICIENT_PAYMENT));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Broker fee is smaller, but still too big once the offer
            // seller's minimum is taken into account.
            env(token::brokerOffers(gw, buyerOfferIndex, audOfferIndex),
                token::brokerFee(gwAUD(1.5)),
                ter(tecINSUFFICIENT_PAYMENT));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Remove buyer's offer.
            env(token::cancelOffer(buyer, {buyerOfferIndex}));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }
        //----------------------------------------------------------------------
        // preclaim buy
        {
            // buyer creates a buy offer for one of alice's nfts.
            uint256 const buyerOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftAlice0ID, gwAUD(30)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Don't accept a buy offer if the sell flag is set.
            env(token::acceptBuyOffer(buyer, plainOfferIndex),
                ter(tecNFTOKEN_OFFER_TYPE_MISMATCH));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 7);

            // An account can't accept its own offer.
            env(token::acceptBuyOffer(buyer, buyerOfferIndex),
                ter(tecCANT_ACCEPT_OWN_NFTOKEN_OFFER));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // An offer acceptor must have enough funds to pay for the offer.
            env(pay(buyer, gw, gwAUD(30)));
            env.close();
            BEAST_EXPECT(env.balance(buyer, gwAUD) == gwAUD(0));
            env(token::acceptBuyOffer(alice, buyerOfferIndex),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // alice gives her NFT to gw, so alice no longer owns nftAlice0.
            {
                uint256 const offerIndex =
                    keylet::nftoffer(alice, env.seq(alice)).key;
                env(token::createOffer(alice, nftAlice0ID, XRP(0)),
                    txflags(tfSellNFToken));
                env.close();
                env(token::acceptSellOffer(gw, offerIndex));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 7);
            }
            env(pay(gw, buyer, gwAUD(30)));
            env.close();

            // alice can't accept a buy offer for an NFT she no longer owns.
            env(token::acceptBuyOffer(alice, buyerOfferIndex),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Remove buyer's offer.
            env(token::cancelOffer(buyer, {buyerOfferIndex}));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }
        //----------------------------------------------------------------------
        // preclaim sell
        {
            // buyer creates a buy offer for one of alice's nfts.
            uint256 const buyerOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftXrpOnlyID, XRP(30)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Don't accept a sell offer without the sell flag set.
            env(token::acceptSellOffer(alice, buyerOfferIndex),
                ter(tecNFTOKEN_OFFER_TYPE_MISMATCH));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 7);

            // An account can't accept its own offer.
            env(token::acceptSellOffer(alice, plainOfferIndex),
                ter(tecCANT_ACCEPT_OWN_NFTOKEN_OFFER));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // The seller must currently be in possession of the token they
            // are selling.  alice gave nftAlice0ID to gw.
            env(token::acceptSellOffer(buyer, plainOfferIndex),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // gw gives nftAlice0ID back to alice.  That allows us to check
            // buyer attempting to accept one of alice's offers with
            // insufficient funds.
            {
                uint256 const offerIndex =
                    keylet::nftoffer(gw, env.seq(gw)).key;
                env(token::createOffer(gw, nftAlice0ID, XRP(0)),
                    txflags(tfSellNFToken));
                env.close();
                env(token::acceptSellOffer(alice, offerIndex));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 7);
            }
            env(pay(buyer, gw, gwAUD(30)));
            env.close();
            BEAST_EXPECT(env.balance(buyer, gwAUD) == gwAUD(0));
            env(token::acceptSellOffer(buyer, audOfferIndex),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);
        }

        //----------------------------------------------------------------------
        // doApply
        //
        // As far as I can see none of the failure modes are accessible as
        // long as the preflight and preclaim conditions are met.
    }

    void
    testMintFlagBurnable(FeatureBitset features)
    {
        // Exercise NFTs with flagBurnable set and not set.
        testcase("Mint flagBurnable");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const buyer{"buyer"};
        Account const minter1{"minter1"};
        Account const minter2{"minter2"};

        env.fund(XRP(1000), alice, buyer, minter1, minter2);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // alice selects minter as her minter.
        env(token::setMinter(alice, minter1));
        env.close();

        // A lambda that...
        //  1. creates an alice nft
        //  2. minted by minter and
        //  3. transfers that nft to buyer.
        auto nftToBuyer = [&env, &alice, &minter1, &buyer](
                              std::uint32_t flags) {
            uint256 const nftID{token::getNextID(env, alice, 0u, flags)};
            env(token::mint(minter1, 0u), token::issuer(alice), txflags(flags));
            env.close();

            uint256 const offerIndex =
                keylet::nftoffer(minter1, env.seq(minter1)).key;
            env(token::createOffer(minter1, nftID, XRP(0)),
                txflags(tfSellNFToken));
            env.close();

            env(token::acceptSellOffer(buyer, offerIndex));
            env.close();

            return nftID;
        };

        // An NFT without flagBurnable can only be burned by its owner.
        {
            uint256 const noBurnID = nftToBuyer(0);
            env(token::burn(alice, noBurnID),
                token::owner(buyer),
                ter(tecNO_PERMISSION));
            env.close();
            env(token::burn(minter1, noBurnID),
                token::owner(buyer),
                ter(tecNO_PERMISSION));
            env.close();
            env(token::burn(minter2, noBurnID),
                token::owner(buyer),
                ter(tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT(ownerCount(env, buyer) == 1);
            env(token::burn(buyer, noBurnID), token::owner(buyer));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // An NFT with flagBurnable can be burned by the issuer.
        {
            uint256 const burnableID = nftToBuyer(tfBurnable);
            env(token::burn(minter2, burnableID),
                token::owner(buyer),
                ter(tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT(ownerCount(env, buyer) == 1);
            env(token::burn(alice, burnableID), token::owner(buyer));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // An NFT with flagBurnable can be burned by the owner.
        {
            uint256 const burnableID = nftToBuyer(tfBurnable);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
            env(token::burn(buyer, burnableID));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // An NFT with flagBurnable can be burned by the minter.
        {
            uint256 const burnableID = nftToBuyer(tfBurnable);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
            env(token::burn(buyer, burnableID), token::owner(buyer));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // An nft with flagBurnable may be burned by the issuers' minter,
        // who may not be the original minter.
        {
            uint256 const burnableID = nftToBuyer(tfBurnable);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            env(token::setMinter(alice, minter2));
            env.close();

            // minter1 is no longer alice's minter, so no longer has
            // permisson to burn alice's nfts.
            env(token::burn(minter1, burnableID),
                token::owner(buyer),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // minter2, however, can burn alice's nfts.
            env(token::burn(minter2, burnableID), token::owner(buyer));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
    }

    void
    testMintFlagOnlyXRP(FeatureBitset features)
    {
        // Exercise NFTs with flagOnlyXRP set and not set.
        testcase("Mint flagOnlyXRP");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const buyer{"buyer"};
        Account const gw("gw");
        IOU const gwAUD(gw["AUD"]);

        // Set trust lines so alice and buyer can use gwAUD.
        env.fund(XRP(1000), alice, buyer, gw);
        env.close();
        env(trust(alice, gwAUD(1000)));
        env(trust(buyer, gwAUD(1000)));
        env.close();
        env(pay(gw, buyer, gwAUD(100)));

        // Don't set flagOnlyXRP and offers can be made with IOUs.
        {
            uint256 const nftIOUsOkayID{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            uint256 const aliceOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftIOUsOkayID, gwAUD(50)),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            BEAST_EXPECT(ownerCount(env, buyer) == 1);
            uint256 const buyerOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftIOUsOkayID, gwAUD(50)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Cancel the two offers just to be tidy.
            env(token::cancelOffer(alice, {aliceOfferIndex}));
            env(token::cancelOffer(buyer, {buyerOfferIndex}));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Also burn alice's nft.
            env(token::burn(alice, nftIOUsOkayID));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
        }

        // Set flagOnlyXRP and offers using IOUs are rejected.
        {
            uint256 const nftOnlyXRPID{
                token::getNextID(env, alice, 0u, tfOnlyXRP | tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfOnlyXRP | tfTransferable));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            env(token::createOffer(alice, nftOnlyXRPID, gwAUD(50)),
                txflags(tfSellNFToken),
                ter(temBAD_AMOUNT));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            BEAST_EXPECT(ownerCount(env, buyer) == 1);
            env(token::createOffer(buyer, nftOnlyXRPID, gwAUD(50)),
                token::owner(alice),
                ter(temBAD_AMOUNT));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // However offers for XRP are okay.
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            env(token::createOffer(alice, nftOnlyXRPID, XRP(60)),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            BEAST_EXPECT(ownerCount(env, buyer) == 1);
            env(token::createOffer(buyer, nftOnlyXRPID, XRP(60)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, buyer) == 2);
        }
    }

    void
    testMintFlagCreateTrustLine(FeatureBitset features)
    {
        // Exercise NFTs with flagCreateTrustLines set and not set.
        testcase("Mint flagCreateTrustLines");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const cheri{"cheri"};
        Account const gw("gw");
        IOU const gwAUD(gw["AUD"]);
        IOU const gwCAD(gw["CAD"]);
        IOU const gwEUR(gw["EUR"]);

        // The behavior of this test changes dramatically based on the
        // presence (or absence) of the fixRemoveNFTokenAutoTrustLine
        // amendment.  So we test both cases here.
        for (auto const& tweakedFeatures :
             {features - fixRemoveNFTokenAutoTrustLine,
              features | fixRemoveNFTokenAutoTrustLine})
        {
            Env env{*this, tweakedFeatures};
            env.fund(XRP(1000), alice, becky, cheri, gw);
            env.close();

            // Set trust lines so becky and cheri can use gw's currency.
            env(trust(becky, gwAUD(1000)));
            env(trust(cheri, gwAUD(1000)));
            env(trust(becky, gwCAD(1000)));
            env(trust(cheri, gwCAD(1000)));
            env(trust(becky, gwEUR(1000)));
            env(trust(cheri, gwEUR(1000)));
            env.close();
            env(pay(gw, becky, gwAUD(500)));
            env(pay(gw, becky, gwCAD(500)));
            env(pay(gw, becky, gwEUR(500)));
            env(pay(gw, cheri, gwAUD(500)));
            env(pay(gw, cheri, gwCAD(500)));
            env.close();

            // An nft without flagCreateTrustLines but with a non-zero transfer
            // fee will not allow creating offers that use IOUs for payment.
            for (std::uint32_t xferFee : {0, 1})
            {
                uint256 const nftNoAutoTrustID{
                    token::getNextID(env, alice, 0u, tfTransferable, xferFee)};
                env(token::mint(alice, 0u),
                    token::xferFee(xferFee),
                    txflags(tfTransferable));
                env.close();

                // becky buys the nft for 1 drop.
                uint256 const beckyBuyOfferIndex =
                    keylet::nftoffer(becky, env.seq(becky)).key;
                env(token::createOffer(becky, nftNoAutoTrustID, drops(1)),
                    token::owner(alice));
                env.close();
                env(token::acceptBuyOffer(alice, beckyBuyOfferIndex));
                env.close();

                // becky attempts to sell the nft for AUD.
                TER const createOfferTER =
                    xferFee ? TER(tecNO_LINE) : TER(tesSUCCESS);
                uint256 const beckyOfferIndex =
                    keylet::nftoffer(becky, env.seq(becky)).key;
                env(token::createOffer(becky, nftNoAutoTrustID, gwAUD(100)),
                    txflags(tfSellNFToken),
                    ter(createOfferTER));
                env.close();

                // cheri offers to buy the nft for CAD.
                uint256 const cheriOfferIndex =
                    keylet::nftoffer(cheri, env.seq(cheri)).key;
                env(token::createOffer(cheri, nftNoAutoTrustID, gwCAD(100)),
                    token::owner(becky),
                    ter(createOfferTER));
                env.close();

                // To keep things tidy, cancel the offers.
                env(token::cancelOffer(becky, {beckyOfferIndex}));
                env(token::cancelOffer(cheri, {cheriOfferIndex}));
                env.close();
            }
            // An nft with flagCreateTrustLines but with a non-zero transfer
            // fee allows transfers using IOUs for payment.
            {
                std::uint16_t transferFee = 10000;  // 10%

                uint256 const nftAutoTrustID{token::getNextID(
                    env, alice, 0u, tfTransferable | tfTrustLine, transferFee)};

                // If the fixRemoveNFTokenAutoTrustLine amendment is active
                // then this transaction fails.
                {
                    TER const mintTER =
                        tweakedFeatures[fixRemoveNFTokenAutoTrustLine]
                        ? static_cast<TER>(temINVALID_FLAG)
                        : static_cast<TER>(tesSUCCESS);

                    env(token::mint(alice, 0u),
                        token::xferFee(transferFee),
                        txflags(tfTransferable | tfTrustLine),
                        ter(mintTER));
                    env.close();

                    // If fixRemoveNFTokenAutoTrustLine is active the rest
                    // of this test falls on its face.
                    if (tweakedFeatures[fixRemoveNFTokenAutoTrustLine])
                        break;
                }
                // becky buys the nft for 1 drop.
                uint256 const beckyBuyOfferIndex =
                    keylet::nftoffer(becky, env.seq(becky)).key;
                env(token::createOffer(becky, nftAutoTrustID, drops(1)),
                    token::owner(alice));
                env.close();
                env(token::acceptBuyOffer(alice, beckyBuyOfferIndex));
                env.close();

                // becky sells the nft for AUD.
                uint256 const beckySellOfferIndex =
                    keylet::nftoffer(becky, env.seq(becky)).key;
                env(token::createOffer(becky, nftAutoTrustID, gwAUD(100)),
                    txflags(tfSellNFToken));
                env.close();
                env(token::acceptSellOffer(cheri, beckySellOfferIndex));
                env.close();

                // alice should now have a trust line for gwAUD.
                BEAST_EXPECT(env.balance(alice, gwAUD) == gwAUD(10));

                // becky buys the nft back for CAD.
                uint256 const beckyBuyBackOfferIndex =
                    keylet::nftoffer(becky, env.seq(becky)).key;
                env(token::createOffer(becky, nftAutoTrustID, gwCAD(50)),
                    token::owner(cheri));
                env.close();
                env(token::acceptBuyOffer(cheri, beckyBuyBackOfferIndex));
                env.close();

                // alice should now have a trust line for gwAUD and gwCAD.
                BEAST_EXPECT(env.balance(alice, gwAUD) == gwAUD(10));
                BEAST_EXPECT(env.balance(alice, gwCAD) == gwCAD(5));
            }
            // Now that alice has trust lines preestablished, an nft without
            // flagCreateTrustLines will work for preestablished trust lines.
            {
                std::uint16_t transferFee = 5000;  // 5%
                uint256 const nftNoAutoTrustID{token::getNextID(
                    env, alice, 0u, tfTransferable, transferFee)};
                env(token::mint(alice, 0u),
                    token::xferFee(transferFee),
                    txflags(tfTransferable));
                env.close();

                // alice sells the nft using AUD.
                uint256 const aliceSellOfferIndex =
                    keylet::nftoffer(alice, env.seq(alice)).key;
                env(token::createOffer(alice, nftNoAutoTrustID, gwAUD(200)),
                    txflags(tfSellNFToken));
                env.close();
                env(token::acceptSellOffer(cheri, aliceSellOfferIndex));
                env.close();

                // alice should now have AUD(210):
                //  o 200 for this sale and
                //  o 10 for the previous sale's fee.
                BEAST_EXPECT(env.balance(alice, gwAUD) == gwAUD(210));

                // cheri can't sell the NFT for EUR, but can for CAD.
                env(token::createOffer(cheri, nftNoAutoTrustID, gwEUR(50)),
                    txflags(tfSellNFToken),
                    ter(tecNO_LINE));
                env.close();
                uint256 const cheriSellOfferIndex =
                    keylet::nftoffer(cheri, env.seq(cheri)).key;
                env(token::createOffer(cheri, nftNoAutoTrustID, gwCAD(100)),
                    txflags(tfSellNFToken));
                env.close();
                env(token::acceptSellOffer(becky, cheriSellOfferIndex));
                env.close();

                // alice should now have CAD(10):
                //  o 5 from this sale's fee and
                //  o 5 for the previous sale's fee.
                BEAST_EXPECT(env.balance(alice, gwCAD) == gwCAD(10));
            }
        }
    }

    void
    testMintFlagTransferable(FeatureBitset features)
    {
        // Exercise NFTs with flagTransferable set and not set.
        testcase("Mint flagTransferable");

        using namespace test::jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const minter{"minter"};

        env.fund(XRP(1000), alice, becky, minter);
        env.close();

        // First try an nft made by alice without flagTransferable set.
        {
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            uint256 const nftAliceNoTransferID{
                token::getNextID(env, alice, 0u)};
            env(token::mint(alice, 0u), token::xferFee(0));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // becky tries to offer to buy alice's nft.
            BEAST_EXPECT(ownerCount(env, becky) == 0);
            env(token::createOffer(becky, nftAliceNoTransferID, XRP(20)),
                token::owner(alice),
                ter(tefNFTOKEN_IS_NOT_TRANSFERABLE));

            // alice offers to sell the nft and becky accepts the offer.
            uint256 const aliceSellOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftAliceNoTransferID, XRP(20)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(becky, aliceSellOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 1);

            // becky tries to offer the nft for sale.
            env(token::createOffer(becky, nftAliceNoTransferID, XRP(21)),
                txflags(tfSellNFToken),
                ter(tefNFTOKEN_IS_NOT_TRANSFERABLE));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 1);

            // becky tries to offer the nft for sale with alice as the
            // destination.  That also doesn't work.
            env(token::createOffer(becky, nftAliceNoTransferID, XRP(21)),
                txflags(tfSellNFToken),
                token::destination(alice),
                ter(tefNFTOKEN_IS_NOT_TRANSFERABLE));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 1);

            // alice offers to buy the nft back from becky.  becky accepts
            // the offer.
            uint256 const aliceBuyOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftAliceNoTransferID, XRP(22)),
                token::owner(becky));
            env.close();
            env(token::acceptBuyOffer(becky, aliceBuyOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, becky) == 0);

            // alice burns her nft so accounting is simpler below.
            env(token::burn(alice, nftAliceNoTransferID));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 0);
        }
        // Try an nft minted by minter for alice without flagTransferable set.
        {
            env(token::setMinter(alice, minter));
            env.close();

            BEAST_EXPECT(ownerCount(env, minter) == 0);
            uint256 const nftMinterNoTransferID{
                token::getNextID(env, alice, 0u)};
            env(token::mint(minter), token::issuer(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, minter) == 1);

            // becky tries to offer to buy minter's nft.
            BEAST_EXPECT(ownerCount(env, becky) == 0);
            env(token::createOffer(becky, nftMinterNoTransferID, XRP(20)),
                token::owner(minter),
                ter(tefNFTOKEN_IS_NOT_TRANSFERABLE));
            env.close();
            BEAST_EXPECT(ownerCount(env, becky) == 0);

            // alice removes authorization of minter.
            env(token::clearMinter(alice));
            env.close();

            // minter tries to offer their nft for sale.
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            env(token::createOffer(minter, nftMinterNoTransferID, XRP(21)),
                txflags(tfSellNFToken),
                ter(tefNFTOKEN_IS_NOT_TRANSFERABLE));
            env.close();
            BEAST_EXPECT(ownerCount(env, minter) == 1);

            // Let enough ledgers pass that old transactions are no longer
            // retried, then alice gives authorization back to minter.
            for (int i = 0; i < 10; ++i)
                env.close();

            env(token::setMinter(alice, minter));
            env.close();
            BEAST_EXPECT(ownerCount(env, minter) == 1);

            // minter successfully offers their nft for sale.
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            uint256 const minterSellOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftMinterNoTransferID, XRP(22)),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, minter) == 2);

            // alice removes authorization of minter so we can see whether
            // minter's pre-existing offer still works.
            env(token::clearMinter(alice));
            env.close();

            // becky buys minter's nft even though minter is no longer alice's
            // official minter.
            BEAST_EXPECT(ownerCount(env, becky) == 0);
            env(token::acceptSellOffer(becky, minterSellOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 0);

            // becky attempts to sell the nft.
            env(token::createOffer(becky, nftMinterNoTransferID, XRP(23)),
                txflags(tfSellNFToken),
                ter(tefNFTOKEN_IS_NOT_TRANSFERABLE));
            env.close();

            // Since minter is not, at the moment, alice's official minter
            // they cannot create an offer to buy the nft they minted.
            BEAST_EXPECT(ownerCount(env, minter) == 0);
            env(token::createOffer(minter, nftMinterNoTransferID, XRP(24)),
                token::owner(becky),
                ter(tefNFTOKEN_IS_NOT_TRANSFERABLE));
            env.close();
            BEAST_EXPECT(ownerCount(env, minter) == 0);

            // alice can create an offer to buy the nft.
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            uint256 const aliceBuyOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftMinterNoTransferID, XRP(25)),
                token::owner(becky));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // Let enough ledgers pass that old transactions are no longer
            // retried, then alice gives authorization back to minter.
            for (int i = 0; i < 10; ++i)
                env.close();

            env(token::setMinter(alice, minter));
            env.close();

            // Now minter can create an offer to buy the nft.
            BEAST_EXPECT(ownerCount(env, minter) == 0);
            uint256 const minterBuyOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftMinterNoTransferID, XRP(26)),
                token::owner(becky));
            env.close();
            BEAST_EXPECT(ownerCount(env, minter) == 1);

            // alice removes authorization of minter so we can see whether
            // minter's pre-existing buy offer still works.
            env(token::clearMinter(alice));
            env.close();

            // becky accepts minter's sell offer.
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            env(token::acceptBuyOffer(becky, minterBuyOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, becky) == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // minter burns their nft and alice cancels her offer so the
            // next tests can start with a clean slate.
            env(token::burn(minter, nftMinterNoTransferID), ter(tesSUCCESS));
            env.close();
            env(token::cancelOffer(alice, {aliceBuyOfferIndex}));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 0);
        }
        // nfts with flagTransferable set should be buyable and salable
        // by anybody.
        {
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            uint256 const nftAliceID{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // Both alice and becky can make offers for alice's nft.
            uint256 const aliceSellOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftAliceID, XRP(20)),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            uint256 const beckyBuyOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftAliceID, XRP(21)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            // becky accepts alice's sell offer.
            env(token::acceptSellOffer(becky, aliceSellOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 2);

            // becky offers to sell the nft.
            uint256 const beckySellOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftAliceID, XRP(22)),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 3);

            // minter buys the nft (even though minter is not currently
            // alice's minter).
            env(token::acceptSellOffer(minter, beckySellOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 1);

            // minter offers to sell the nft.
            uint256 const minterSellOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftAliceID, XRP(23)),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 2);

            // alice buys back the nft.
            env(token::acceptSellOffer(alice, minterSellOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 0);

            // Remember the buy offer that becky made for alice's token way
            // back when?  It's still in the ledger, and alice accepts it.
            env(token::acceptBuyOffer(alice, beckyBuyOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 0);

            // Just for tidyness, becky burns the token before shutting
            // things down.
            env(token::burn(becky, nftAliceID));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, becky) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 0);
        }
    }

    void
    testMintTransferFee(FeatureBitset features)
    {
        // Exercise NFTs with and without a transferFee.
        testcase("Mint transferFee");

        using namespace test::jtx;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;

        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const carol{"carol"};
        Account const minter{"minter"};
        Account const gw{"gw"};
        IOU const gwXAU(gw["XAU"]);

        env.fund(XRP(1000), alice, becky, carol, minter, gw);
        env.close();

        env(trust(alice, gwXAU(2000)));
        env(trust(becky, gwXAU(2000)));
        env(trust(carol, gwXAU(2000)));
        env(trust(minter, gwXAU(2000)));
        env.close();
        env(pay(gw, alice, gwXAU(1000)));
        env(pay(gw, becky, gwXAU(1000)));
        env(pay(gw, carol, gwXAU(1000)));
        env(pay(gw, minter, gwXAU(1000)));
        env.close();

        // Giving alice a minter helps us see if transfer rates are affected
        // by that.
        env(token::setMinter(alice, minter));
        env.close();

        // If there is no transferFee, then alice gets nothing for the
        // transfer.
        {
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, carol) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 1);

            uint256 const nftID =
                token::getNextID(env, alice, 0u, tfTransferable);
            env(token::mint(alice), txflags(tfTransferable));
            env.close();

            // Becky buys the nft for XAU(10).  Check balances.
            uint256 const beckyBuyOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftID, gwXAU(10)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1000));

            env(token::acceptBuyOffer(alice, beckyBuyOfferIndex));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1010));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(990));

            // becky sells nft to carol.  alice's balance should not change.
            uint256 const beckySellOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftID, gwXAU(10)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(carol, beckySellOfferIndex));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1010));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(990));

            // minter buys nft from carol.  alice's balance should not change.
            uint256 const minterBuyOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, gwXAU(10)),
                token::owner(carol));
            env.close();
            env(token::acceptBuyOffer(carol, minterBuyOfferIndex));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1010));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(990));

            // minter sells the nft to alice.  gwXAU balances should finish
            // where they started.
            uint256 const minterSellOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, gwXAU(10)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(alice, minterSellOfferIndex));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));

            // alice burns the nft to make later tests easier to think about.
            env(token::burn(alice, nftID));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, carol) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
        }

        // Set the smallest possible transfer fee.
        {
            // An nft with a transfer fee of 1 basis point.
            uint256 const nftID =
                token::getNextID(env, alice, 0u, tfTransferable, 1);
            env(token::mint(alice), txflags(tfTransferable), token::xferFee(1));
            env.close();

            // Becky buys the nft for XAU(10).  Check balances.
            uint256 const beckyBuyOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftID, gwXAU(10)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1000));

            env(token::acceptBuyOffer(alice, beckyBuyOfferIndex));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1010));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(990));

            // becky sells nft to carol.  alice's balance goes up.
            uint256 const beckySellOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftID, gwXAU(10)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(carol, beckySellOfferIndex));
            env.close();

            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1010.0001));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(999.9999));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(990));

            // minter buys nft from carol.  alice's balance goes up.
            uint256 const minterBuyOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, gwXAU(10)),
                token::owner(carol));
            env.close();
            env(token::acceptBuyOffer(carol, minterBuyOfferIndex));
            env.close();

            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1010.0002));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(999.9999));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(999.9999));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(990));

            // minter sells the nft to alice.  Because alice is part of the
            // transaction no transfer fee is removed.
            uint256 const minterSellOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, gwXAU(10)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(alice, minterSellOfferIndex));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1000.0002));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(999.9999));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(999.9999));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));

            // alice pays to becky and carol so subsequent tests are easier
            // to think about.
            env(pay(alice, becky, gwXAU(0.0001)));
            env(pay(alice, carol, gwXAU(0.0001)));
            env.close();

            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));

            // alice burns the nft to make later tests easier to think about.
            env(token::burn(alice, nftID));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, carol) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
        }

        // Set the largest allowed transfer fee.
        {
            // A transfer fee greater than 50% is not allowed.
            env(token::mint(alice),
                txflags(tfTransferable),
                token::xferFee(maxTransferFee + 1),
                ter(temBAD_NFTOKEN_TRANSFER_FEE));
            env.close();

            // Make an nft with a transfer fee of 50%.
            uint256 const nftID = token::getNextID(
                env, alice, 0u, tfTransferable, maxTransferFee);
            env(token::mint(alice),
                txflags(tfTransferable),
                token::xferFee(maxTransferFee));
            env.close();

            // Becky buys the nft for XAU(10).  Check balances.
            uint256 const beckyBuyOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftID, gwXAU(10)),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1000));

            env(token::acceptBuyOffer(alice, beckyBuyOfferIndex));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1010));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(990));

            // becky sells nft to minter.  alice's balance goes up.
            uint256 const beckySellOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftID, gwXAU(100)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(minter, beckySellOfferIndex));
            env.close();

            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1060));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1040));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(900));

            // carol buys nft from minter.  alice's balance goes up.
            uint256 const carolBuyOfferIndex =
                keylet::nftoffer(carol, env.seq(carol)).key;
            env(token::createOffer(carol, nftID, gwXAU(10)),
                token::owner(minter));
            env.close();
            env(token::acceptBuyOffer(minter, carolBuyOfferIndex));
            env.close();

            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1065));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1040));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(905));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(990));

            // carol sells the nft to alice.  Because alice is part of the
            // transaction no transfer fee is removed.
            uint256 const carolSellOfferIndex =
                keylet::nftoffer(carol, env.seq(carol)).key;
            env(token::createOffer(carol, nftID, gwXAU(10)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(alice, carolSellOfferIndex));
            env.close();

            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1055));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1040));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(905));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(1000));

            // rebalance so subsequent tests are easier to think about.
            env(pay(alice, minter, gwXAU(55)));
            env(pay(becky, minter, gwXAU(40)));
            env.close();
            BEAST_EXPECT(env.balance(alice, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(becky, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(carol, gwXAU) == gwXAU(1000));
            BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));

            // alice burns the nft to make later tests easier to think about.
            env(token::burn(alice, nftID));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, becky) == 1);
            BEAST_EXPECT(ownerCount(env, carol) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
        }

        // See the impact of rounding when the nft is sold for small amounts
        // of drops.
        for (auto NumberSwitchOver : {true})
        {
            if (NumberSwitchOver)
                env.enableFeature(fixUniversalNumber);
            else
                env.disableFeature(fixUniversalNumber);

            // An nft with a transfer fee of 1 basis point.
            uint256 const nftID =
                token::getNextID(env, alice, 0u, tfTransferable, 1);
            env(token::mint(alice), txflags(tfTransferable), token::xferFee(1));
            env.close();

            // minter buys the nft for XRP(1).  Since the transfer involves
            // alice there should be no transfer fee.
            STAmount aliceBalance = env.balance(alice);
            STAmount minterBalance = env.balance(minter);
            uint256 const minterBuyOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, XRP(1)), token::owner(alice));
            env.close();
            env(token::acceptBuyOffer(alice, minterBuyOfferIndex));
            env.close();
            aliceBalance += XRP(1) - baseFee;
            minterBalance -= XRP(1) + baseFee;
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(minter) == minterBalance);

            // minter sells to carol.  The payment is just small enough that
            // alice does not get any transfer fee.
            auto pmt = NumberSwitchOver ? drops(50000) : drops(99999);
            STAmount carolBalance = env.balance(carol);
            uint256 const minterSellOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, pmt), txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(carol, minterSellOfferIndex));
            env.close();
            minterBalance += pmt - baseFee;
            carolBalance -= pmt + baseFee;
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(minter) == minterBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);

            // carol sells to becky. This is the smallest amount to pay for a
            // transfer that enables a transfer fee of 1 basis point.
            STAmount beckyBalance = env.balance(becky);
            uint256 const beckyBuyOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            pmt = NumberSwitchOver ? drops(50001) : drops(100000);
            env(token::createOffer(becky, nftID, pmt), token::owner(carol));
            env.close();
            env(token::acceptBuyOffer(carol, beckyBuyOfferIndex));
            env.close();
            carolBalance += pmt - drops(1) - baseFee;
            beckyBalance -= pmt + baseFee;
            aliceBalance += drops(1);

            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(minter) == minterBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);
            BEAST_EXPECT(env.balance(becky) == beckyBalance);
        }

        // See the impact of rounding when the nft is sold for small amounts
        // of an IOU.
        {
            // An nft with a transfer fee of 1 basis point.
            uint256 const nftID =
                token::getNextID(env, alice, 0u, tfTransferable, 1);
            env(token::mint(alice), txflags(tfTransferable), token::xferFee(1));
            env.close();

            // Due to the floating point nature of IOUs we need to
            // significantly reduce the gwXAU balances of our accounts prior
            // to the iou transfer.  Otherwise no transfers will happen.
            env(pay(alice, gw, env.balance(alice, gwXAU)));
            env(pay(minter, gw, env.balance(minter, gwXAU)));
            env(pay(becky, gw, env.balance(becky, gwXAU)));
            env.close();

            STAmount const startXAUBalance(
                gwXAU.issue(), STAmount::cMinValue, STAmount::cMinOffset + 5);
            env(pay(gw, alice, startXAUBalance));
            env(pay(gw, minter, startXAUBalance));
            env(pay(gw, becky, startXAUBalance));
            env.close();

            // Here is the smallest expressible gwXAU amount.
            STAmount tinyXAU(
                gwXAU.issue(), STAmount::cMinValue, STAmount::cMinOffset);

            // minter buys the nft for tinyXAU.  Since the transfer involves
            // alice there should be no transfer fee.
            STAmount aliceBalance = env.balance(alice, gwXAU);
            STAmount minterBalance = env.balance(minter, gwXAU);
            uint256 const minterBuyOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, tinyXAU),
                token::owner(alice));
            env.close();
            env(token::acceptBuyOffer(alice, minterBuyOfferIndex));
            env.close();
            aliceBalance += tinyXAU;
            minterBalance -= tinyXAU;
            BEAST_EXPECT(env.balance(alice, gwXAU) == aliceBalance);
            BEAST_EXPECT(env.balance(minter, gwXAU) == minterBalance);

            // minter sells to carol.
            STAmount carolBalance = env.balance(carol, gwXAU);
            uint256 const minterSellOfferIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, tinyXAU),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(carol, minterSellOfferIndex));
            env.close();

            minterBalance += tinyXAU;
            carolBalance -= tinyXAU;
            // tiny XAU is so small that alice does not get a transfer fee.
            BEAST_EXPECT(env.balance(alice, gwXAU) == aliceBalance);
            BEAST_EXPECT(env.balance(minter, gwXAU) == minterBalance);
            BEAST_EXPECT(env.balance(carol, gwXAU) == carolBalance);

            // carol sells to becky.  This is the smallest gwXAU amount
            // to pay for a transfer that enables a transfer fee of 1.
            STAmount const cheapNFT(
                gwXAU.issue(), STAmount::cMinValue, STAmount::cMinOffset + 5);

            STAmount beckyBalance = env.balance(becky, gwXAU);
            uint256 const beckyBuyOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftID, cheapNFT),
                token::owner(carol));
            env.close();
            env(token::acceptBuyOffer(carol, beckyBuyOfferIndex));
            env.close();

            aliceBalance += tinyXAU;
            beckyBalance -= cheapNFT;
            carolBalance += cheapNFT - tinyXAU;
            BEAST_EXPECT(env.balance(alice, gwXAU) == aliceBalance);
            BEAST_EXPECT(env.balance(minter, gwXAU) == minterBalance);
            BEAST_EXPECT(env.balance(carol, gwXAU) == carolBalance);
            BEAST_EXPECT(env.balance(becky, gwXAU) == beckyBalance);
        }
    }

    void
    testMintTaxon(FeatureBitset features)
    {
        // Exercise the NFT taxon field.
        testcase("Mint taxon");

        using namespace test::jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const becky{"becky"};

        env.fund(XRP(1000), alice, becky);
        env.close();

        // The taxon field is incorporated straight into the NFT ID.  So
        // tests only need to operate on NFT IDs; we don't need to generate
        // any transactions.

        // The taxon value should be recoverable from the NFT ID.
        {
            uint256 const nftID = token::getNextID(env, alice, 0u);
            BEAST_EXPECT(nft::getTaxon(nftID) == nft::toTaxon(0));
        }

        // Make sure the full range of taxon values work.  We just tried
        // the minimum.  Now try the largest.
        {
            uint256 const nftID = token::getNextID(env, alice, 0xFFFFFFFFu);
            BEAST_EXPECT(nft::getTaxon(nftID) == nft::toTaxon((0xFFFFFFFF)));
        }

        // Do some touch testing to show that the taxon is recoverable no
        // matter what else changes around it in the nft ID.
        {
            std::uint32_t const taxon = rand_int<std::uint32_t>();
            for (int i = 0; i < 10; ++i)
            {
                // lambda to produce a useful message on error.
                auto check = [this](std::uint32_t taxon, uint256 const& nftID) {
                    nft::Taxon const gotTaxon = nft::getTaxon(nftID);
                    if (nft::toTaxon(taxon) == gotTaxon)
                        pass();
                    else
                    {
                        std::stringstream ss;
                        ss << "Taxon recovery failed from nftID "
                           << to_string(nftID) << ".  Expected: " << taxon
                           << "; got: " << gotTaxon;
                        fail(ss.str());
                    }
                };

                uint256 const nftAliceID = token::getID(
                    env,
                    alice,
                    taxon,
                    rand_int<std::uint32_t>(),
                    rand_int<std::uint16_t>(),
                    rand_int<std::uint16_t>());
                check(taxon, nftAliceID);

                uint256 const nftBeckyID = token::getID(
                    env,
                    becky,
                    taxon,
                    rand_int<std::uint32_t>(),
                    rand_int<std::uint16_t>(),
                    rand_int<std::uint16_t>());
                check(taxon, nftBeckyID);
            }
        }
    }

    void
    testMintURI(FeatureBitset features)
    {
        // Exercise the NFT URI field.
        //  1. Create a number of NFTs with and without URIs.
        //  2. Retrieve the NFTs from the server.
        //  3. Make sure the right URI is attached to each NFT.
        testcase("Mint URI");

        using namespace test::jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const becky{"becky"};

        env.fund(XRP(10000), alice, becky);
        env.close();

        // lambda that returns a randomly generated string which fits
        // the constraints of a URI.  Empty strings may be returned.
        // In the empty string case do not add the URI to the nft.
        auto randURI = []() {
            std::string ret;

            // About 20% of the returned strings should be empty
            if (rand_int(4) == 0)
                return ret;

            std::size_t const strLen = rand_int(256);
            ret.reserve(strLen);
            for (std::size_t i = 0; i < strLen; ++i)
                ret.push_back(rand_byte());

            return ret;
        };

        // Make a list of URIs that we'll put in nfts.
        struct Entry
        {
            std::string uri;
            std::uint32_t taxon;

            Entry(std::string uri_, std::uint32_t taxon_)
                : uri(std::move(uri_)), taxon(taxon_)
            {
            }
        };

        std::vector<Entry> entries;
        entries.reserve(100);
        for (std::size_t i = 0; i < 100; ++i)
            entries.emplace_back(randURI(), rand_int<std::uint32_t>());

        // alice creates nfts using entries.
        for (Entry const& entry : entries)
        {
            if (entry.uri.empty())
            {
                env(token::mint(alice, entry.taxon));
            }
            else
            {
                env(token::mint(alice, entry.taxon), token::uri(entry.uri));
            }
            env.close();
        }

        // Recover alice's nfts from the ledger.
        Json::Value aliceNFTs = [&env, &alice]() {
            Json::Value params;
            params[jss::account] = alice.human();
            params[jss::type] = "state";
            return env.rpc("json", "account_nfts", to_string(params));
        }();

        // Verify that the returned NFTs match what we sent.
        Json::Value& nfts = aliceNFTs[jss::result][jss::account_nfts];
        if (!BEAST_EXPECT(nfts.size() == entries.size()))
            return;

        // Sort the returned NFTs by nft_serial so the are in the same order
        // as entries.
        std::vector<Json::Value> sortedNFTs;
        sortedNFTs.reserve(nfts.size());
        for (std::size_t i = 0; i < nfts.size(); ++i)
            sortedNFTs.push_back(nfts[i]);
        std::sort(
            sortedNFTs.begin(),
            sortedNFTs.end(),
            [](Json::Value const& lhs, Json::Value const& rhs) {
                return lhs[jss::nft_serial] < rhs[jss::nft_serial];
            });

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            Entry const& entry = entries[i];
            Json::Value const& ret = sortedNFTs[i];
            BEAST_EXPECT(entry.taxon == ret[sfNFTokenTaxon.jsonName]);
            if (entry.uri.empty())
            {
                BEAST_EXPECT(!ret.isMember(sfURI.jsonName));
            }
            else
            {
                BEAST_EXPECT(strHex(entry.uri) == ret[sfURI.jsonName]);
            }
        }
    }

    void
    testCreateOfferDestination(FeatureBitset features)
    {
        // Explore the CreateOffer Destination field.
        testcase("Create offer destination");

        using namespace test::jtx;

        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const minter{"minter"};
        Account const buyer{"buyer"};
        Account const broker{"broker"};

        env.fund(XRP(1000), issuer, minter, buyer, broker);

        // We want to explore how issuers vs minters fits into the permission
        // scheme.  So issuer issues and minter mints.
        env(token::setMinter(issuer, minter));
        env.close();

        uint256 const nftokenID =
            token::getNextID(env, issuer, 0, tfTransferable);
        env(token::mint(minter, 0),
            token::issuer(issuer),
            txflags(tfTransferable));
        env.close();

        // Test how adding a Destination field to an offer affects permissions
        // for canceling offers.
        {
            uint256 const offerMinterToIssuer =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(issuer),
                txflags(tfSellNFToken));

            uint256 const offerMinterToBuyer =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(buyer),
                txflags(tfSellNFToken));

            uint256 const offerIssuerToMinter =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftokenID, drops(1)),
                token::owner(minter),
                token::destination(minter));

            uint256 const offerIssuerToBuyer =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftokenID, drops(1)),
                token::owner(minter),
                token::destination(buyer));

            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 2);
            BEAST_EXPECT(ownerCount(env, minter) == 3);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);

            // Test who gets to cancel the offers.  Anyone outside of the
            // offer-owner/destination pair should not be able to cancel the
            // offers.
            //
            // Note that issuer does not have any special permissions regarding
            // offer cancellation.  issuer cannot cancel an offer for an
            // NFToken they issued.
            env(token::cancelOffer(issuer, {offerMinterToBuyer}),
                ter(tecNO_PERMISSION));
            env(token::cancelOffer(buyer, {offerMinterToIssuer}),
                ter(tecNO_PERMISSION));
            env(token::cancelOffer(buyer, {offerIssuerToMinter}),
                ter(tecNO_PERMISSION));
            env(token::cancelOffer(minter, {offerIssuerToBuyer}),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 2);
            BEAST_EXPECT(ownerCount(env, minter) == 3);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);

            // Both the offer creator and and destination should be able to
            // cancel the offers.
            env(token::cancelOffer(buyer, {offerMinterToBuyer}));
            env(token::cancelOffer(minter, {offerMinterToIssuer}));
            env(token::cancelOffer(buyer, {offerIssuerToBuyer}));
            env(token::cancelOffer(issuer, {offerIssuerToMinter}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }

        // Test how adding a Destination field to a sell offer affects
        // accepting that offer.
        {
            uint256 const offerMinterSellsToBuyer =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(buyer),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);

            // issuer cannot accept a sell offer where they are not the
            // destination.
            env(token::acceptSellOffer(issuer, offerMinterSellsToBuyer),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);

            // However buyer can accept the sell offer.
            env(token::acceptSellOffer(buyer, offerMinterSellsToBuyer));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 0);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }

        // Test how adding a Destination field to a buy offer affects
        // accepting that offer.
        {
            uint256 const offerMinterBuysFromBuyer =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::owner(buyer),
                token::destination(buyer));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // issuer cannot accept a buy offer where they are the
            // destination.
            env(token::acceptBuyOffer(issuer, offerMinterBuysFromBuyer),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Buyer accepts minter's offer.
            env(token::acceptBuyOffer(buyer, offerMinterBuysFromBuyer));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);

            // If a destination other than the NFToken owner is set, that
            // destination must act as a broker.  The NFToken owner may not
            // simply accept the offer.
            uint256 const offerBuyerBuysFromMinter =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID, drops(1)),
                token::owner(minter),
                token::destination(broker));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            env(token::acceptBuyOffer(minter, offerBuyerBuysFromMinter),
                ter(tecNO_PERMISSION));
            env.close();

            // Clean up the unused offer.
            env(token::cancelOffer(buyer, {offerBuyerBuysFromMinter}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }

        // Show that a sell offer's Destination can broker that sell offer
        // to another account.
        {
            uint256 const offerMinterToBroker =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(broker),
                txflags(tfSellNFToken));

            uint256 const offerBuyerToMinter =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID, drops(1)),
                token::owner(minter));

            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            {
                // issuer cannot broker the offers, because they are not the
                // Destination.
                TER const expectTer = features[fixNonFungibleTokensV1_2]
                    ? tecNO_PERMISSION
                    : tecNFTOKEN_BUY_SELL_MISMATCH;
                env(token::brokerOffers(
                        issuer, offerBuyerToMinter, offerMinterToBroker),
                    ter(expectTer));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 0);
                BEAST_EXPECT(ownerCount(env, minter) == 2);
                BEAST_EXPECT(ownerCount(env, buyer) == 1);
            }

            // Since broker is the sell offer's destination, they can broker
            // the two offers.
            env(token::brokerOffers(
                broker, offerBuyerToMinter, offerMinterToBroker));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 0);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }

        // Show that brokered mode cannot complete a transfer where the
        // Destination doesn't match, but can complete if the Destination
        // does match.
        {
            uint256 const offerBuyerToMinter =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID, drops(1)),
                token::destination(minter),
                txflags(tfSellNFToken));

            uint256 const offerMinterToBuyer =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::owner(buyer));

            uint256 const offerIssuerToBuyer =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftokenID, drops(1)),
                token::owner(buyer));

            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            {
                // Cannot broker offers when the sell destination is not the
                // buyer.
                TER const expectTer = features[fixNonFungibleTokensV1_2]
                    ? tecNO_PERMISSION
                    : tecNFTOKEN_BUY_SELL_MISMATCH;
                env(token::brokerOffers(
                        broker, offerIssuerToBuyer, offerBuyerToMinter),
                    ter(expectTer));
                env.close();

                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(ownerCount(env, minter) == 1);
                BEAST_EXPECT(ownerCount(env, buyer) == 2);

                // amendment switch: When enabled the broker fails, when
                // disabled the broker succeeds if the destination is the buyer.
                TER const eexpectTer = features[fixNonFungibleTokensV1_2]
                    ? tecNO_PERMISSION
                    : TER(tesSUCCESS);
                env(token::brokerOffers(
                        broker, offerMinterToBuyer, offerBuyerToMinter),
                    ter(eexpectTer));
                env.close();

                if (features[fixNonFungibleTokensV1_2])
                    // Buyer is successful with acceptOffer.
                    env(token::acceptBuyOffer(buyer, offerMinterToBuyer));
                env.close();

                // Clean out the unconsumed offer.
                env(token::cancelOffer(buyer, {offerBuyerToMinter}));
                env.close();

                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(ownerCount(env, minter) == 1);
                BEAST_EXPECT(ownerCount(env, buyer) == 0);

                // Clean out the unconsumed offer.
                env(token::cancelOffer(issuer, {offerIssuerToBuyer}));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 0);
                BEAST_EXPECT(ownerCount(env, minter) == 1);
                BEAST_EXPECT(ownerCount(env, buyer) == 0);
                return;
            }
        }

        // Show that if a buy and a sell offer both have the same destination,
        // then that destination can broker the offers.
        {
            uint256 const offerMinterToBroker =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(broker),
                txflags(tfSellNFToken));

            uint256 const offerBuyerToBroker =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID, drops(1)),
                token::owner(minter),
                token::destination(broker));

            {
                // Cannot broker offers when the sell destination is not the
                // buyer or the broker.
                TER const expectTer = features[fixNonFungibleTokensV1_2]
                    ? tecNO_PERMISSION
                    : tecNFTOKEN_BUY_SELL_MISMATCH;
                env(token::brokerOffers(
                        issuer, offerBuyerToBroker, offerMinterToBroker),
                    ter(expectTer));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 0);
                BEAST_EXPECT(ownerCount(env, minter) == 2);
                BEAST_EXPECT(ownerCount(env, buyer) == 1);
            }

            // Broker is successful if they are the destination of both offers.
            env(token::brokerOffers(
                broker, offerBuyerToBroker, offerMinterToBroker));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 0);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }
    }

    void
    testCreateOfferDestinationDisallowIncoming(FeatureBitset features)
    {
        testcase("Create offer destination disallow incoming");

        using namespace test::jtx;

        // test flag doesn't set unless amendment enabled
        {
            Env env{*this, features - disallowIncoming};
            Account const alice{"alice"};
            env.fund(XRP(10000), alice);
            env(fset(alice, asfDisallowIncomingNFTokenOffer));
            env.close();
            auto const sle = env.le(alice);
            uint32_t flags = sle->getFlags();
            BEAST_EXPECT(!(flags & lsfDisallowIncomingNFTokenOffer));
        }

        Env env{*this, features | disallowIncoming};

        Account const issuer{"issuer"};
        Account const minter{"minter"};
        Account const buyer{"buyer"};
        Account const alice{"alice"};

        env.fund(XRP(1000), issuer, minter, buyer, alice);

        env(token::setMinter(issuer, minter));
        env.close();

        uint256 const nftokenID =
            token::getNextID(env, issuer, 0, tfTransferable);
        env(token::mint(minter, 0),
            token::issuer(issuer),
            txflags(tfTransferable));
        env.close();

        // enable flag
        env(fset(buyer, asfDisallowIncomingNFTokenOffer));
        env.close();

        // a sell offer from the minter to the buyer should be rejected
        {
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(buyer),
                txflags(tfSellNFToken),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }

        // disable the flag
        env(fclear(buyer, asfDisallowIncomingNFTokenOffer));
        env.close();

        // create offer (allowed now) then cancel
        {
            uint256 const offerIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;

            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(buyer),
                txflags(tfSellNFToken));
            env.close();

            env(token::cancelOffer(minter, {offerIndex}));
            env.close();
        }

        // create offer, enable flag, then cancel
        {
            uint256 const offerIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;

            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(buyer),
                txflags(tfSellNFToken));
            env.close();

            env(fset(buyer, asfDisallowIncomingNFTokenOffer));
            env.close();

            env(token::cancelOffer(minter, {offerIndex}));
            env.close();

            env(fclear(buyer, asfDisallowIncomingNFTokenOffer));
            env.close();
        }

        // create offer then transfer
        {
            uint256 const offerIndex =
                keylet::nftoffer(minter, env.seq(minter)).key;

            env(token::createOffer(minter, nftokenID, drops(1)),
                token::destination(buyer),
                txflags(tfSellNFToken));
            env.close();

            env(token::acceptSellOffer(buyer, offerIndex));
            env.close();
        }

        // buyer now owns the token

        // enable flag again
        env(fset(buyer, asfDisallowIncomingNFTokenOffer));
        env.close();

        // a random offer to buy the token
        {
            env(token::createOffer(alice, nftokenID, drops(1)),
                token::owner(buyer),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // minter offer to buy the token
        {
            env(token::createOffer(minter, nftokenID, drops(1)),
                token::owner(buyer),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // minter mint and offer to buyer
        if (features[featureNFTokenMintOffer])
        {
            // enable flag
            env(fset(buyer, asfDisallowIncomingNFTokenOffer));
            // a sell offer from the minter to the buyer should be rejected
            env(token::mint(minter),
                token::amount(drops(1)),
                token::destination(buyer),
                ter(tecNO_PERMISSION));
            env.close();

            // disable flag
            env(fclear(buyer, asfDisallowIncomingNFTokenOffer));
            env(token::mint(minter),
                token::amount(drops(1)),
                token::destination(buyer));
            env.close();
        }
    }

    void
    testCreateOfferExpiration(FeatureBitset features)
    {
        // Explore the CreateOffer Expiration field.
        testcase("Create offer expiration");

        using namespace test::jtx;

        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const minter{"minter"};
        Account const buyer{"buyer"};

        env.fund(XRP(1000), issuer, minter, buyer);

        // We want to explore how issuers vs minters fits into the permission
        // scheme.  So issuer issues and minter mints.
        env(token::setMinter(issuer, minter));
        env.close();

        uint256 const nftokenID0 =
            token::getNextID(env, issuer, 0, tfTransferable);
        env(token::mint(minter, 0),
            token::issuer(issuer),
            txflags(tfTransferable));
        env.close();

        uint256 const nftokenID1 =
            token::getNextID(env, issuer, 0, tfTransferable);
        env(token::mint(minter, 0),
            token::issuer(issuer),
            txflags(tfTransferable));
        env.close();

        // Test how adding an Expiration field to an offer affects permissions
        // for cancelling offers.
        {
            std::uint32_t const expiration = lastClose(env) + 25;

            uint256 const offerMinterToIssuer =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID0, drops(1)),
                token::destination(issuer),
                token::expiration(expiration),
                txflags(tfSellNFToken));

            uint256 const offerMinterToAnyone =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID0, drops(1)),
                token::expiration(expiration),
                txflags(tfSellNFToken));

            uint256 const offerIssuerToMinter =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftokenID0, drops(1)),
                token::owner(minter),
                token::expiration(expiration));

            uint256 const offerBuyerToMinter =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, drops(1)),
                token::owner(minter),
                token::expiration(expiration));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 3);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Test who gets to cancel the offers.  Anyone outside of the
            // offer-owner/destination pair should not be able to cancel
            // unexpired offers.
            //
            // Note that these are tec responses, so these transactions will
            // not be retried by the ledger.
            env(token::cancelOffer(issuer, {offerMinterToAnyone}),
                ter(tecNO_PERMISSION));
            env(token::cancelOffer(buyer, {offerIssuerToMinter}),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(lastClose(env) < expiration);
            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 3);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // The offer creator can cancel their own unexpired offer.
            env(token::cancelOffer(minter, {offerMinterToAnyone}));

            // The destination of a sell offer can cancel the NFT owner's
            // unexpired offer.
            env(token::cancelOffer(issuer, {offerMinterToIssuer}));

            // Close enough ledgers to get past the expiration.
            while (lastClose(env) < expiration)
                env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Anyone can cancel expired offers.
            env(token::cancelOffer(issuer, {offerBuyerToMinter}));
            env(token::cancelOffer(buyer, {offerIssuerToMinter}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // Show that:
        //  1. An unexpired sell offer with an expiration can be accepted.
        //  2. An expired sell offer cannot be accepted and remains
        //     in ledger after the accept fails.
        {
            std::uint32_t const expiration = lastClose(env) + 25;

            uint256 const offer0 =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID0, drops(1)),
                token::expiration(expiration),
                txflags(tfSellNFToken));

            uint256 const offer1 =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID1, drops(1)),
                token::expiration(expiration),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(lastClose(env) < expiration);
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 3);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);

            // Anyone can accept an unexpired sell offer.
            env(token::acceptSellOffer(buyer, offer0));

            // Close enough ledgers to get past the expiration.
            while (lastClose(env) < expiration)
                env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // No one can accept an expired sell offer.
            env(token::acceptSellOffer(buyer, offer1), ter(tecEXPIRED));
            env(token::acceptSellOffer(issuer, offer1), ter(tecEXPIRED));
            env.close();

            // The expired sell offer is still in the ledger.
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Anyone can cancel the expired sell offer.
            env(token::cancelOffer(issuer, {offer1}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Transfer nftokenID0 back to minter so we start the next test in
            // a simple place.
            uint256 const offerSellBack =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, XRP(0)),
                txflags(tfSellNFToken),
                token::destination(minter));
            env.close();
            env(token::acceptSellOffer(minter, offerSellBack));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // Show that:
        //  1. An unexpired buy offer with an expiration can be accepted.
        //  2. An expired buy offer cannot be accepted and remains
        //     in ledger after the accept fails.
        {
            std::uint32_t const expiration = lastClose(env) + 25;

            uint256 const offer0 = keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, drops(1)),
                token::owner(minter),
                token::expiration(expiration));

            uint256 const offer1 = keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID1, drops(1)),
                token::owner(minter),
                token::expiration(expiration));
            env.close();
            BEAST_EXPECT(lastClose(env) < expiration);
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // An unexpired buy offer can be accepted.
            env(token::acceptBuyOffer(minter, offer0));

            // Close enough ledgers to get past the expiration.
            while (lastClose(env) < expiration)
                env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // An expired buy offer cannot be accepted.
            env(token::acceptBuyOffer(minter, offer1), ter(tecEXPIRED));
            env(token::acceptBuyOffer(issuer, offer1), ter(tecEXPIRED));
            env.close();

            // The expired buy offer is still in the ledger.
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Anyone can cancel the expired buy offer.
            env(token::cancelOffer(issuer, {offer1}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Transfer nftokenID0 back to minter so we start the next test in
            // a simple place.
            uint256 const offerSellBack =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, XRP(0)),
                txflags(tfSellNFToken),
                token::destination(minter));
            env.close();
            env(token::acceptSellOffer(minter, offerSellBack));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // Show that in brokered mode:
        //  1. An unexpired sell offer with an expiration can be accepted.
        //  2. An expired sell offer cannot be accepted and remains
        //     in ledger after the accept fails.
        {
            std::uint32_t const expiration = lastClose(env) + 25;

            uint256 const sellOffer0 =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID0, drops(1)),
                token::expiration(expiration),
                txflags(tfSellNFToken));

            uint256 const sellOffer1 =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID1, drops(1)),
                token::expiration(expiration),
                txflags(tfSellNFToken));

            uint256 const buyOffer0 =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, drops(1)),
                token::owner(minter));

            uint256 const buyOffer1 =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID1, drops(1)),
                token::owner(minter));

            env.close();
            BEAST_EXPECT(lastClose(env) < expiration);
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 3);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // An unexpired offer can be brokered.
            env(token::brokerOffers(issuer, buyOffer0, sellOffer0));

            // Close enough ledgers to get past the expiration.
            while (lastClose(env) < expiration)
                env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // If the sell offer is expired it cannot be brokered.
            env(token::brokerOffers(issuer, buyOffer1, sellOffer1),
                ter(tecEXPIRED));
            env.close();

            // The expired sell offer is still in the ledger.
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Anyone can cancel the expired sell offer.
            env(token::cancelOffer(buyer, {buyOffer1, sellOffer1}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Transfer nftokenID0 back to minter so we start the next test in
            // a simple place.
            uint256 const offerSellBack =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, XRP(0)),
                txflags(tfSellNFToken),
                token::destination(minter));
            env.close();
            env(token::acceptSellOffer(minter, offerSellBack));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // Show that in brokered mode:
        //  1. An unexpired buy offer with an expiration can be accepted.
        //  2. An expired buy offer cannot be accepted and remains
        //     in ledger after the accept fails.
        {
            std::uint32_t const expiration = lastClose(env) + 25;

            uint256 const sellOffer0 =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID0, drops(1)),
                txflags(tfSellNFToken));

            uint256 const sellOffer1 =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID1, drops(1)),
                txflags(tfSellNFToken));

            uint256 const buyOffer0 =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, drops(1)),
                token::expiration(expiration),
                token::owner(minter));

            uint256 const buyOffer1 =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID1, drops(1)),
                token::expiration(expiration),
                token::owner(minter));

            env.close();
            BEAST_EXPECT(lastClose(env) < expiration);
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 3);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // An unexpired offer can be brokered.
            env(token::brokerOffers(issuer, buyOffer0, sellOffer0));

            // Close enough ledgers to get past the expiration.
            while (lastClose(env) < expiration)
                env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // If the buy offer is expired it cannot be brokered.
            env(token::brokerOffers(issuer, buyOffer1, sellOffer1),
                ter(tecEXPIRED));
            env.close();

            // The expired buy offer is still in the ledger.
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Anyone can cancel the expired buy offer.
            env(token::cancelOffer(minter, {buyOffer1, sellOffer1}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Transfer nftokenID0 back to minter so we start the next test in
            // a simple place.
            uint256 const offerSellBack =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, XRP(0)),
                txflags(tfSellNFToken),
                token::destination(minter));
            env.close();
            env(token::acceptSellOffer(minter, offerSellBack));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
        // Show that in brokered mode:
        //  1. An unexpired buy/sell offer pair with an expiration can be
        //     accepted.
        //  2. An expired buy/sell offer pair cannot be accepted and they
        //     remain in ledger after the accept fails.
        {
            std::uint32_t const expiration = lastClose(env) + 25;

            uint256 const sellOffer0 =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID0, drops(1)),
                token::expiration(expiration),
                txflags(tfSellNFToken));

            uint256 const sellOffer1 =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftokenID1, drops(1)),
                token::expiration(expiration),
                txflags(tfSellNFToken));

            uint256 const buyOffer0 =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, drops(1)),
                token::expiration(expiration),
                token::owner(minter));

            uint256 const buyOffer1 =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID1, drops(1)),
                token::expiration(expiration),
                token::owner(minter));

            env.close();
            BEAST_EXPECT(lastClose(env) < expiration);
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 3);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Unexpired offers can be brokered.
            env(token::brokerOffers(issuer, buyOffer0, sellOffer0));

            // Close enough ledgers to get past the expiration.
            while (lastClose(env) < expiration)
                env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // If the offers are expired they cannot be brokered.
            env(token::brokerOffers(issuer, buyOffer1, sellOffer1),
                ter(tecEXPIRED));
            env.close();

            // The expired offers are still in the ledger.
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // Anyone can cancel the expired offers.
            env(token::cancelOffer(issuer, {buyOffer1, sellOffer1}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // Transfer nftokenID0 back to minter so we start the next test in
            // a simple place.
            uint256 const offerSellBack =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftokenID0, XRP(0)),
                txflags(tfSellNFToken),
                token::destination(minter));
            env.close();
            env(token::acceptSellOffer(minter, offerSellBack));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 0);
        }
    }

    void
    testCancelOffers(FeatureBitset features)
    {
        // Look at offer canceling.
        testcase("Cancel offers");

        using namespace test::jtx;

        Env env{*this, features};

        Account const alice("alice");
        Account const becky("becky");
        Account const minter("minter");
        env.fund(XRP(50000), alice, becky, minter);
        env.close();

        // alice has a minter to see if minters have offer canceling permission.
        env(token::setMinter(alice, minter));
        env.close();

        uint256 const nftokenID =
            token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0), txflags(tfTransferable));
        env.close();

        // Anyone can cancel an expired offer.
        uint256 const expiredOfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;

        env(token::createOffer(alice, nftokenID, XRP(1000)),
            txflags(tfSellNFToken),
            token::expiration(lastClose(env) + 13));
        env.close();

        // The offer has not expired yet, so becky can't cancel it now.
        BEAST_EXPECT(ownerCount(env, alice) == 2);
        env(token::cancelOffer(becky, {expiredOfferIndex}),
            ter(tecNO_PERMISSION));
        env.close();

        // Close a couple of ledgers and advance the time.  Then becky
        // should be able to cancel the (now) expired offer.
        env.close();
        env.close();
        env(token::cancelOffer(becky, {expiredOfferIndex}));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // Create a couple of offers with a destination.  Those offers
        // should be cancellable by the creator and the destination.
        uint256 const dest1OfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;

        env(token::createOffer(alice, nftokenID, XRP(1000)),
            token::destination(becky),
            txflags(tfSellNFToken));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        // Minter can't cancel that offer, but becky (the destination) can.
        env(token::cancelOffer(minter, {dest1OfferIndex}),
            ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        env(token::cancelOffer(becky, {dest1OfferIndex}));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // alice can cancel her own offer, even if becky is the destination.
        uint256 const dest2OfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;

        env(token::createOffer(alice, nftokenID, XRP(1000)),
            token::destination(becky),
            txflags(tfSellNFToken));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        env(token::cancelOffer(alice, {dest2OfferIndex}));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // The issuer has no special permissions regarding offer cancellation.
        // Minter creates a token with alice as issuer.  alice cannot cancel
        // minter's offer.
        uint256 const mintersNFTokenID =
            token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(minter, 0),
            token::issuer(alice),
            txflags(tfTransferable));
        env.close();

        uint256 const minterOfferIndex =
            keylet::nftoffer(minter, env.seq(minter)).key;

        env(token::createOffer(minter, mintersNFTokenID, XRP(1000)),
            txflags(tfSellNFToken));
        env.close();
        BEAST_EXPECT(ownerCount(env, minter) == 2);

        // Nobody other than minter should be able to cancel minter's offer.
        env(token::cancelOffer(alice, {minterOfferIndex}),
            ter(tecNO_PERMISSION));
        env(token::cancelOffer(becky, {minterOfferIndex}),
            ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(ownerCount(env, minter) == 2);

        env(token::cancelOffer(minter, {minterOfferIndex}));
        env.close();
        BEAST_EXPECT(ownerCount(env, minter) == 1);
    }

    void
    testCancelTooManyOffers(FeatureBitset features)
    {
        // Look at the case where too many offers are passed in a cancel.
        testcase("Cancel too many offers");

        using namespace test::jtx;

        Env env{*this, features};

        // We want to maximize the metadata from a cancel offer transaction to
        // make sure we don't hit metadata limits.  The way we'll do that is:
        //
        //  1. Generate twice as many separate funded accounts as we have
        //     offers.
        //  2.
        //     a. One of these accounts mints an NFT with a full URL.
        //     b. The other account makes an offer that will expire soon.
        //  3. After all of these offers have expired, cancel all of the
        //     expired offers in a single transaction.
        //
        // I can't think of any way to increase the metadata beyond this,
        // but I'm open to ideas.
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        env.close();

        std::string const uri(maxTokenURILength, '?');
        std::vector<uint256> offerIndexes;
        offerIndexes.reserve(maxTokenOfferCancelCount + 1);
        for (uint32_t i = 0; i < maxTokenOfferCancelCount + 1; ++i)
        {
            Account const nftAcct(std::string("nftAcct") + std::to_string(i));
            Account const offerAcct(
                std::string("offerAcct") + std::to_string(i));
            env.fund(XRP(1000), nftAcct, offerAcct);
            env.close();

            uint256 const nftokenID =
                token::getNextID(env, nftAcct, 0, tfTransferable);
            env(token::mint(nftAcct, 0),
                token::uri(uri),
                txflags(tfTransferable));
            env.close();

            offerIndexes.push_back(
                keylet::nftoffer(offerAcct, env.seq(offerAcct)).key);
            env(token::createOffer(offerAcct, nftokenID, drops(1)),
                token::owner(nftAcct),
                token::expiration(lastClose(env) + 5));
            env.close();
        }

        // Close the ledger so the last of the offers expire.
        env.close();

        // All offers should be in the ledger.
        for (uint256 const& offerIndex : offerIndexes)
        {
            BEAST_EXPECT(env.le(keylet::nftoffer(offerIndex)));
        }

        // alice attempts to cancel all of the expired offers.  There is one
        // too many so the request fails.
        env(token::cancelOffer(alice, offerIndexes), ter(temMALFORMED));
        env.close();

        // However alice can cancel just one of the offers.
        env(token::cancelOffer(alice, {offerIndexes.back()}));
        env.close();

        // Verify that offer is gone from the ledger.
        BEAST_EXPECT(!env.le(keylet::nftoffer(offerIndexes.back())));
        offerIndexes.pop_back();

        // But alice adds a sell offer to the list...
        {
            uint256 const nftokenID =
                token::getNextID(env, alice, 0, tfTransferable);
            env(token::mint(alice, 0),
                token::uri(uri),
                txflags(tfTransferable));
            env.close();

            offerIndexes.push_back(keylet::nftoffer(alice, env.seq(alice)).key);
            env(token::createOffer(alice, nftokenID, drops(1)),
                txflags(tfSellNFToken));
            env.close();

            // alice's owner count should now to 2 for the nft and the offer.
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            // Because alice added the sell offer there are still too many
            // offers in the list to cancel.
            env(token::cancelOffer(alice, offerIndexes), ter(temMALFORMED));
            env.close();

            // alice burns her nft which removes the nft and the offer.
            env(token::burn(alice, nftokenID));
            env.close();

            // If alice's owner count is zero we can see that the offer
            // and nft are both gone.
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            offerIndexes.pop_back();
        }

        // Now there are few enough offers in the list that they can all
        // be cancelled in a single transaction.
        env(token::cancelOffer(alice, offerIndexes));
        env.close();

        // Verify that remaining offers are gone from the ledger.
        for (uint256 const& offerIndex : offerIndexes)
        {
            BEAST_EXPECT(!env.le(keylet::nftoffer(offerIndex)));
        }
    }

    void
    testBrokeredAccept(FeatureBitset features)
    {
        // Look at the case where too many offers are passed in a cancel.
        testcase("Brokered NFT offer accept");

        using namespace test::jtx;

        for (auto const& tweakedFeatures :
             {features - fixNonFungibleTokensV1_2,
              features | fixNonFungibleTokensV1_2})
        {
            Env env{*this, tweakedFeatures};
            auto const baseFee = env.current()->fees().base;

            // The most important thing to explore here is the way funds are
            // assigned from the buyer to...
            //  o the Seller,
            //  o the Broker, and
            //  o the Issuer (in the case of a transfer fee).

            Account const issuer{"issuer"};
            Account const minter{"minter"};
            Account const buyer{"buyer"};
            Account const broker{"broker"};
            Account const gw{"gw"};
            IOU const gwXAU(gw["XAU"]);

            env.fund(XRP(1000), issuer, minter, buyer, broker, gw);
            env.close();

            env(trust(issuer, gwXAU(2000)));
            env(trust(minter, gwXAU(2000)));
            env(trust(buyer, gwXAU(2000)));
            env(trust(broker, gwXAU(2000)));
            env.close();

            env(token::setMinter(issuer, minter));
            env.close();

            // Lambda to check owner count of all accounts is one.
            auto checkOwnerCountIsOne =
                [this, &env](
                    std::initializer_list<std::reference_wrapper<Account const>>
                        accounts,
                    int line) {
                    for (Account const& acct : accounts)
                    {
                        if (std::uint32_t ownerCount =
                                test::jtx::ownerCount(env, acct);
                            ownerCount != 1)
                        {
                            std::stringstream ss;
                            ss << "Account " << acct.human()
                               << " expected ownerCount == 1.  Got "
                               << ownerCount;
                            fail(ss.str(), __FILE__, line);
                        }
                    }
                };

            // Lambda that mints an NFT and returns the nftID.
            auto mintNFT = [&env, &issuer, &minter](std::uint16_t xferFee = 0) {
                uint256 const nftID =
                    token::getNextID(env, issuer, 0, tfTransferable, xferFee);
                env(token::mint(minter, 0),
                    token::issuer(issuer),
                    token::xferFee(xferFee),
                    txflags(tfTransferable));
                env.close();
                return nftID;
            };

            // o Seller is selling for zero XRP.
            // o Broker charges no fee.
            // o No transfer fee.
            //
            // Since minter is selling for zero the currency must be XRP.
            {
                checkOwnerCountIsOne({issuer, minter, buyer, broker}, __LINE__);

                uint256 const nftID = mintNFT();

                // minter creates their offer.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nftID, XRP(0)),
                    txflags(tfSellNFToken));
                env.close();

                // buyer creates their offer.  Note: a buy offer can never
                // offer zero.
                uint256 const buyOfferIndex =
                    keylet::nftoffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, XRP(1)),
                    token::owner(minter));
                env.close();

                auto const minterBalance = env.balance(minter);
                auto const buyerBalance = env.balance(buyer);
                auto const brokerBalance = env.balance(broker);
                auto const issuerBalance = env.balance(issuer);

                // Broker charges no brokerFee.
                env(token::brokerOffers(
                    broker, buyOfferIndex, minterOfferIndex));
                env.close();

                // Note that minter's XRP balance goes up even though they
                // requested XRP(0).
                BEAST_EXPECT(env.balance(minter) == minterBalance + XRP(1));
                BEAST_EXPECT(env.balance(buyer) == buyerBalance - XRP(1));
                BEAST_EXPECT(env.balance(broker) == brokerBalance - baseFee);
                BEAST_EXPECT(env.balance(issuer) == issuerBalance);

                // Burn the NFT so the next test starts with a clean state.
                env(token::burn(buyer, nftID));
                env.close();
            }

            // o Seller is selling for zero XRP.
            // o Broker charges a fee.
            // o No transfer fee.
            //
            // Since minter is selling for zero the currency must be XRP.
            {
                checkOwnerCountIsOne({issuer, minter, buyer, broker}, __LINE__);

                uint256 const nftID = mintNFT();

                // minter creates their offer.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nftID, XRP(0)),
                    txflags(tfSellNFToken));
                env.close();

                // buyer creates their offer.  Note: a buy offer can never
                // offer zero.
                uint256 const buyOfferIndex =
                    keylet::nftoffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, XRP(1)),
                    token::owner(minter));
                env.close();

                // Broker attempts to charge a 1.1 XRP brokerFee and fails.
                env(token::brokerOffers(
                        broker, buyOfferIndex, minterOfferIndex),
                    token::brokerFee(XRP(1.1)),
                    ter(tecINSUFFICIENT_PAYMENT));
                env.close();

                auto const minterBalance = env.balance(minter);
                auto const buyerBalance = env.balance(buyer);
                auto const brokerBalance = env.balance(broker);
                auto const issuerBalance = env.balance(issuer);

                // Broker charges a 0.5 XRP brokerFee.
                env(token::brokerOffers(
                        broker, buyOfferIndex, minterOfferIndex),
                    token::brokerFee(XRP(0.5)));
                env.close();

                // Note that minter's XRP balance goes up even though they
                // requested XRP(0).
                BEAST_EXPECT(env.balance(minter) == minterBalance + XRP(0.5));
                BEAST_EXPECT(env.balance(buyer) == buyerBalance - XRP(1));
                BEAST_EXPECT(
                    env.balance(broker) == brokerBalance + XRP(0.5) - baseFee);
                BEAST_EXPECT(env.balance(issuer) == issuerBalance);

                // Burn the NFT so the next test starts with a clean state.
                env(token::burn(buyer, nftID));
                env.close();
            }

            // o Seller is selling for zero XRP.
            // o Broker charges no fee.
            // o 50% transfer fee.
            //
            // Since minter is selling for zero the currency must be XRP.
            {
                checkOwnerCountIsOne({issuer, minter, buyer, broker}, __LINE__);

                uint256 const nftID = mintNFT(maxTransferFee);

                // minter creates their offer.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nftID, XRP(0)),
                    txflags(tfSellNFToken));
                env.close();

                // buyer creates their offer.  Note: a buy offer can never
                // offer zero.
                uint256 const buyOfferIndex =
                    keylet::nftoffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, XRP(1)),
                    token::owner(minter));
                env.close();

                auto const minterBalance = env.balance(minter);
                auto const buyerBalance = env.balance(buyer);
                auto const brokerBalance = env.balance(broker);
                auto const issuerBalance = env.balance(issuer);

                // Broker charges no brokerFee.
                env(token::brokerOffers(
                    broker, buyOfferIndex, minterOfferIndex));
                env.close();

                // Note that minter's XRP balance goes up even though they
                // requested XRP(0).
                BEAST_EXPECT(env.balance(minter) == minterBalance + XRP(0.5));
                BEAST_EXPECT(env.balance(buyer) == buyerBalance - XRP(1));
                BEAST_EXPECT(env.balance(broker) == brokerBalance - baseFee);
                BEAST_EXPECT(env.balance(issuer) == issuerBalance + XRP(0.5));

                // Burn the NFT so the next test starts with a clean state.
                env(token::burn(buyer, nftID));
                env.close();
            }

            // o Seller is selling for zero XRP.
            // o Broker charges 0.5 XRP.
            // o 50% transfer fee.
            //
            // Since minter is selling for zero the currency must be XRP.
            {
                checkOwnerCountIsOne({issuer, minter, buyer, broker}, __LINE__);

                uint256 const nftID = mintNFT(maxTransferFee);

                // minter creates their offer.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nftID, XRP(0)),
                    txflags(tfSellNFToken));
                env.close();

                // buyer creates their offer.  Note: a buy offer can never
                // offer zero.
                uint256 const buyOfferIndex =
                    keylet::nftoffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, XRP(1)),
                    token::owner(minter));
                env.close();

                auto const minterBalance = env.balance(minter);
                auto const buyerBalance = env.balance(buyer);
                auto const brokerBalance = env.balance(broker);
                auto const issuerBalance = env.balance(issuer);

                // Broker charges a 0.75 XRP brokerFee.
                env(token::brokerOffers(
                        broker, buyOfferIndex, minterOfferIndex),
                    token::brokerFee(XRP(0.75)));
                env.close();

                // Note that, with a 50% transfer fee, issuer gets 1/2 of what's
                // left _after_ broker takes their fee.  minter gets the
                // remainder after both broker and minter take their cuts
                BEAST_EXPECT(env.balance(minter) == minterBalance + XRP(0.125));
                BEAST_EXPECT(env.balance(buyer) == buyerBalance - XRP(1));
                BEAST_EXPECT(
                    env.balance(broker) == brokerBalance + XRP(0.75) - baseFee);
                BEAST_EXPECT(env.balance(issuer) == issuerBalance + XRP(0.125));

                // Burn the NFT so the next test starts with a clean state.
                env(token::burn(buyer, nftID));
                env.close();
            }

            // Lambda to set the balance of all passed in accounts to
            // gwXAU(amount).
            auto setXAUBalance =
                [this, &gw, &gwXAU, &env](
                    std::initializer_list<std::reference_wrapper<Account const>>
                        accounts,
                    int amount,
                    int line) {
                    for (Account const& acct : accounts)
                    {
                        auto const xauAmt = gwXAU(amount);
                        auto const balance = env.balance(acct, gwXAU);
                        if (balance < xauAmt)
                        {
                            env(pay(gw, acct, xauAmt - balance));
                            env.close();
                        }
                        else if (balance > xauAmt)
                        {
                            env(pay(acct, gw, balance - xauAmt));
                            env.close();
                        }
                        if (env.balance(acct, gwXAU) != xauAmt)
                        {
                            std::stringstream ss;
                            ss << "Unable to set " << acct.human()
                               << " account balance to gwXAU(" << amount << ")";
                            this->fail(ss.str(), __FILE__, line);
                        }
                    }
                };

            // The buyer and seller have identical amounts and there is no
            // transfer fee.
            {
                checkOwnerCountIsOne({issuer, minter, buyer, broker}, __LINE__);
                setXAUBalance({issuer, minter, buyer, broker}, 1000, __LINE__);

                uint256 const nftID = mintNFT();

                // minter creates their offer.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nftID, gwXAU(1000)),
                    txflags(tfSellNFToken));
                env.close();

                {
                    // buyer creates an offer for more XAU than they currently
                    // own.
                    uint256 const buyOfferIndex =
                        keylet::nftoffer(buyer, env.seq(buyer)).key;
                    env(token::createOffer(buyer, nftID, gwXAU(1001)),
                        token::owner(minter));
                    env.close();

                    // broker attempts to broker the offers but cannot.
                    env(token::brokerOffers(
                            broker, buyOfferIndex, minterOfferIndex),
                        ter(tecINSUFFICIENT_FUNDS));
                    env.close();

                    // Cancel buyer's bad offer so the next test starts in a
                    // clean state.
                    env(token::cancelOffer(buyer, {buyOfferIndex}));
                    env.close();
                }
                {
                    // buyer creates an offer for less that what minter is
                    // asking.
                    uint256 const buyOfferIndex =
                        keylet::nftoffer(buyer, env.seq(buyer)).key;
                    env(token::createOffer(buyer, nftID, gwXAU(999)),
                        token::owner(minter));
                    env.close();

                    // broker attempts to broker the offers but cannot.
                    env(token::brokerOffers(
                            broker, buyOfferIndex, minterOfferIndex),
                        ter(tecINSUFFICIENT_PAYMENT));
                    env.close();

                    // Cancel buyer's bad offer so the next test starts in a
                    // clean state.
                    env(token::cancelOffer(buyer, {buyOfferIndex}));
                    env.close();
                }

                // buyer creates a large enough offer.
                uint256 const buyOfferIndex =
                    keylet::nftoffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, gwXAU(1000)),
                    token::owner(minter));
                env.close();

                // Broker attempts to charge a brokerFee but cannot.
                env(token::brokerOffers(
                        broker, buyOfferIndex, minterOfferIndex),
                    token::brokerFee(gwXAU(0.1)),
                    ter(tecINSUFFICIENT_PAYMENT));
                env.close();

                // broker charges no brokerFee and succeeds.
                env(token::brokerOffers(
                    broker, buyOfferIndex, minterOfferIndex));
                env.close();

                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(ownerCount(env, minter) == 1);
                BEAST_EXPECT(ownerCount(env, buyer) == 2);
                BEAST_EXPECT(ownerCount(env, broker) == 1);
                BEAST_EXPECT(env.balance(issuer, gwXAU) == gwXAU(1000));
                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(2000));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(broker, gwXAU) == gwXAU(1000));

                // Burn the NFT so the next test starts with a clean state.
                env(token::burn(buyer, nftID));
                env.close();
            }

            // seller offers more than buyer is asking.
            // There are both transfer and broker fees.
            {
                checkOwnerCountIsOne({issuer, minter, buyer, broker}, __LINE__);
                setXAUBalance({issuer, minter, buyer, broker}, 1000, __LINE__);

                uint256 const nftID = mintNFT(maxTransferFee);

                // minter creates their offer.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nftID, gwXAU(900)),
                    txflags(tfSellNFToken));
                env.close();
                {
                    // buyer creates an offer for more XAU than they currently
                    // own.
                    uint256 const buyOfferIndex =
                        keylet::nftoffer(buyer, env.seq(buyer)).key;
                    env(token::createOffer(buyer, nftID, gwXAU(1001)),
                        token::owner(minter));
                    env.close();

                    // broker attempts to broker the offers but cannot.
                    env(token::brokerOffers(
                            broker, buyOfferIndex, minterOfferIndex),
                        ter(tecINSUFFICIENT_FUNDS));
                    env.close();

                    // Cancel buyer's bad offer so the next test starts in a
                    // clean state.
                    env(token::cancelOffer(buyer, {buyOfferIndex}));
                    env.close();
                }
                {
                    // buyer creates an offer for less that what minter is
                    // asking.
                    uint256 const buyOfferIndex =
                        keylet::nftoffer(buyer, env.seq(buyer)).key;
                    env(token::createOffer(buyer, nftID, gwXAU(899)),
                        token::owner(minter));
                    env.close();

                    // broker attempts to broker the offers but cannot.
                    env(token::brokerOffers(
                            broker, buyOfferIndex, minterOfferIndex),
                        ter(tecINSUFFICIENT_PAYMENT));
                    env.close();

                    // Cancel buyer's bad offer so the next test starts in a
                    // clean state.
                    env(token::cancelOffer(buyer, {buyOfferIndex}));
                    env.close();
                }
                // buyer creates a large enough offer.
                uint256 const buyOfferIndex =
                    keylet::nftoffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, gwXAU(1000)),
                    token::owner(minter));
                env.close();

                // Broker attempts to charge a brokerFee larger than the
                // difference between the two offers but cannot.
                env(token::brokerOffers(
                        broker, buyOfferIndex, minterOfferIndex),
                    token::brokerFee(gwXAU(101)),
                    ter(tecINSUFFICIENT_PAYMENT));
                env.close();

                // broker charges the full difference between the two offers and
                // succeeds.
                env(token::brokerOffers(
                        broker, buyOfferIndex, minterOfferIndex),
                    token::brokerFee(gwXAU(100)));
                env.close();

                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(ownerCount(env, minter) == 1);
                BEAST_EXPECT(ownerCount(env, buyer) == 2);
                BEAST_EXPECT(ownerCount(env, broker) == 1);
                BEAST_EXPECT(env.balance(issuer, gwXAU) == gwXAU(1450));
                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1450));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(broker, gwXAU) == gwXAU(1100));

                // Burn the NFT so the next test starts with a clean state.
                env(token::burn(buyer, nftID));
                env.close();
            }
            // seller offers more than buyer is asking.
            // There are both transfer and broker fees, but broker takes less
            // than the maximum.
            {
                checkOwnerCountIsOne({issuer, minter, buyer, broker}, __LINE__);
                setXAUBalance({issuer, minter, buyer, broker}, 1000, __LINE__);

                uint256 const nftID = mintNFT(maxTransferFee / 2);  // 25%

                // minter creates their offer.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nftID, gwXAU(900)),
                    txflags(tfSellNFToken));
                env.close();

                // buyer creates a large enough offer.
                uint256 const buyOfferIndex =
                    keylet::nftoffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, gwXAU(1000)),
                    token::owner(minter));
                env.close();

                // broker charges half difference between the two offers and
                // succeeds.  25% of the remaining difference goes to issuer.
                // The rest goes to minter.
                env(token::brokerOffers(
                        broker, buyOfferIndex, minterOfferIndex),
                    token::brokerFee(gwXAU(50)));
                env.close();

                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(ownerCount(env, minter) == 1);
                BEAST_EXPECT(ownerCount(env, buyer) == 2);
                BEAST_EXPECT(ownerCount(env, broker) == 1);
                BEAST_EXPECT(env.balance(issuer, gwXAU) == gwXAU(1237.5));
                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1712.5));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(broker, gwXAU) == gwXAU(1050));

                // Burn the NFT so the next test starts with a clean state.
                env(token::burn(buyer, nftID));
                env.close();
            }
            // Broker has a balance less than the seller offer
            {
                checkOwnerCountIsOne({issuer, minter, buyer, broker}, __LINE__);
                setXAUBalance({issuer, minter, buyer}, 1000, __LINE__);
                setXAUBalance({broker}, 500, __LINE__);
                uint256 const nftID = mintNFT(maxTransferFee / 2);  // 25%

                // minter creates their offer.
                uint256 const minterOfferIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;
                env(token::createOffer(minter, nftID, gwXAU(900)),
                    txflags(tfSellNFToken));
                env.close();

                // buyer creates a large enough offer.
                uint256 const buyOfferIndex =
                    keylet::nftoffer(buyer, env.seq(buyer)).key;
                env(token::createOffer(buyer, nftID, gwXAU(1000)),
                    token::owner(minter));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                {
                    env(token::brokerOffers(
                            broker, buyOfferIndex, minterOfferIndex),
                        token::brokerFee(gwXAU(50)));
                    env.close();
                    BEAST_EXPECT(ownerCount(env, issuer) == 1);
                    BEAST_EXPECT(ownerCount(env, minter) == 1);
                    BEAST_EXPECT(ownerCount(env, buyer) == 2);
                    BEAST_EXPECT(ownerCount(env, broker) == 1);
                    BEAST_EXPECT(env.balance(issuer, gwXAU) == gwXAU(1237.5));
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1712.5));
                    BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(0));
                    BEAST_EXPECT(env.balance(broker, gwXAU) == gwXAU(550));

                    // Burn the NFT so the next test starts with a clean state.
                    env(token::burn(buyer, nftID));
                    env.close();
                }
                else
                {
                    env(token::brokerOffers(
                            broker, buyOfferIndex, minterOfferIndex),
                        token::brokerFee(gwXAU(50)),
                        ter(tecINSUFFICIENT_FUNDS));
                    env.close();
                    BEAST_EXPECT(ownerCount(env, issuer) == 1);
                    BEAST_EXPECT(ownerCount(env, minter) == 3);
                    BEAST_EXPECT(ownerCount(env, buyer) == 2);
                    BEAST_EXPECT(ownerCount(env, broker) == 1);
                    BEAST_EXPECT(env.balance(issuer, gwXAU) == gwXAU(1000));
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));
                    BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(1000));
                    BEAST_EXPECT(env.balance(broker, gwXAU) == gwXAU(500));

                    // Burn the NFT so the next test starts with a clean state.
                    env(token::burn(minter, nftID));
                    env.close();
                }
            }
        }
    }

    void
    testNFTokenOfferOwner(FeatureBitset features)
    {
        // Verify the Owner field of an offer behaves as expected.
        testcase("NFToken offer owner");

        using namespace test::jtx;

        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const buyer1{"buyer1"};
        Account const buyer2{"buyer2"};
        env.fund(XRP(10000), issuer, buyer1, buyer2);
        env.close();

        // issuer creates an NFT.
        uint256 const nftId{token::getNextID(env, issuer, 0u, tfTransferable)};
        env(token::mint(issuer, 0u), txflags(tfTransferable));
        env.close();

        // Prove that issuer now owns nftId.
        BEAST_EXPECT(nftCount(env, issuer) == 1);
        BEAST_EXPECT(nftCount(env, buyer1) == 0);
        BEAST_EXPECT(nftCount(env, buyer2) == 0);

        // Both buyer1 and buyer2 create buy offers for nftId.
        uint256 const buyer1OfferIndex =
            keylet::nftoffer(buyer1, env.seq(buyer1)).key;
        env(token::createOffer(buyer1, nftId, XRP(100)), token::owner(issuer));
        uint256 const buyer2OfferIndex =
            keylet::nftoffer(buyer2, env.seq(buyer2)).key;
        env(token::createOffer(buyer2, nftId, XRP(100)), token::owner(issuer));
        env.close();

        // Lambda that counts the number of buy offers for a given NFT.
        auto nftBuyOfferCount = [&env](uint256 const& nftId) -> std::size_t {
            // We know that in this case not very many offers will be
            // returned, so we skip the marker stuff.
            Json::Value params;
            params[jss::nft_id] = to_string(nftId);
            Json::Value buyOffers =
                env.rpc("json", "nft_buy_offers", to_string(params));

            if (buyOffers.isMember(jss::result) &&
                buyOffers[jss::result].isMember(jss::offers))
                return buyOffers[jss::result][jss::offers].size();

            return 0;
        };

        // Show there are two buy offers for nftId.
        BEAST_EXPECT(nftBuyOfferCount(nftId) == 2);

        // issuer accepts buyer1's offer.
        env(token::acceptBuyOffer(issuer, buyer1OfferIndex));
        env.close();

        // Prove that buyer1 now owns nftId.
        BEAST_EXPECT(nftCount(env, issuer) == 0);
        BEAST_EXPECT(nftCount(env, buyer1) == 1);
        BEAST_EXPECT(nftCount(env, buyer2) == 0);

        // buyer1's offer was consumed, but buyer2's offer is still in the
        // ledger.
        BEAST_EXPECT(nftBuyOfferCount(nftId) == 1);

        // buyer1 can now accept buyer2's offer, even though buyer2's
        // NFTokenCreateOffer transaction specified the NFT Owner as issuer.
        env(token::acceptBuyOffer(buyer1, buyer2OfferIndex));
        env.close();

        // Prove that buyer2 now owns nftId.
        BEAST_EXPECT(nftCount(env, issuer) == 0);
        BEAST_EXPECT(nftCount(env, buyer1) == 0);
        BEAST_EXPECT(nftCount(env, buyer2) == 1);

        // All of the NFTokenOffers are now consumed.
        BEAST_EXPECT(nftBuyOfferCount(nftId) == 0);
    }

    void
    testNFTokenWithTickets(FeatureBitset features)
    {
        // Make sure all NFToken transactions work with tickets.
        testcase("NFToken transactions with tickets");

        using namespace test::jtx;

        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const buyer{"buyer"};
        env.fund(XRP(10000), issuer, buyer);
        env.close();

        // issuer and buyer grab enough tickets for all of the following
        // transactions.  Note that once the tickets are acquired issuer's
        // and buyer's account sequence numbers should not advance.
        std::uint32_t issuerTicketSeq{env.seq(issuer) + 1};
        env(ticket::create(issuer, 10));
        env.close();
        std::uint32_t const issuerSeq{env.seq(issuer)};
        BEAST_EXPECT(ticketCount(env, issuer) == 10);

        std::uint32_t buyerTicketSeq{env.seq(buyer) + 1};
        env(ticket::create(buyer, 10));
        env.close();
        std::uint32_t const buyerSeq{env.seq(buyer)};
        BEAST_EXPECT(ticketCount(env, buyer) == 10);

        // NFTokenMint
        BEAST_EXPECT(ownerCount(env, issuer) == 10);
        uint256 const nftId{token::getNextID(env, issuer, 0u, tfTransferable)};
        env(token::mint(issuer, 0u),
            txflags(tfTransferable),
            ticket::use(issuerTicketSeq++));
        env.close();
        BEAST_EXPECT(ownerCount(env, issuer) == 10);
        BEAST_EXPECT(ticketCount(env, issuer) == 9);

        // NFTokenCreateOffer
        BEAST_EXPECT(ownerCount(env, buyer) == 10);
        uint256 const offerIndex0 = keylet::nftoffer(buyer, buyerTicketSeq).key;
        env(token::createOffer(buyer, nftId, XRP(1)),
            token::owner(issuer),
            ticket::use(buyerTicketSeq++));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 10);
        BEAST_EXPECT(ticketCount(env, buyer) == 9);

        // NFTokenCancelOffer
        env(token::cancelOffer(buyer, {offerIndex0}),
            ticket::use(buyerTicketSeq++));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 8);
        BEAST_EXPECT(ticketCount(env, buyer) == 8);

        // NFTokenCreateOffer.  buyer tries again.
        uint256 const offerIndex1 = keylet::nftoffer(buyer, buyerTicketSeq).key;
        env(token::createOffer(buyer, nftId, XRP(2)),
            token::owner(issuer),
            ticket::use(buyerTicketSeq++));
        env.close();
        BEAST_EXPECT(ownerCount(env, buyer) == 8);
        BEAST_EXPECT(ticketCount(env, buyer) == 7);

        // NFTokenAcceptOffer.  issuer accepts buyer's offer.
        env(token::acceptBuyOffer(issuer, offerIndex1),
            ticket::use(issuerTicketSeq++));
        env.close();
        BEAST_EXPECT(ownerCount(env, issuer) == 8);
        BEAST_EXPECT(ownerCount(env, buyer) == 8);
        BEAST_EXPECT(ticketCount(env, issuer) == 8);

        // NFTokenBurn.  buyer burns the token they just bought.
        env(token::burn(buyer, nftId), ticket::use(buyerTicketSeq++));
        env.close();
        BEAST_EXPECT(ownerCount(env, issuer) == 8);
        BEAST_EXPECT(ownerCount(env, buyer) == 6);
        BEAST_EXPECT(ticketCount(env, buyer) == 6);

        // Verify that the account sequence numbers did not advance.
        BEAST_EXPECT(env.seq(issuer) == issuerSeq);
        BEAST_EXPECT(env.seq(buyer) == buyerSeq);
    }

    void
    testNFTokenDeleteAccount(FeatureBitset features)
    {
        // Account deletion rules with NFTs:
        //  1. An account holding one or more NFT offers may be deleted.
        //  2. An NFT issuer with any NFTs they have issued still in the
        //     ledger may not be deleted.
        //  3. An account holding one or more NFTs may not be deleted.
        testcase("NFToken delete account");

        using namespace test::jtx;

        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const minter{"minter"};
        Account const becky{"becky"};
        Account const carla{"carla"};
        Account const daria{"daria"};

        env.fund(XRP(10000), issuer, minter, becky, carla, daria);
        env.close();

        // Allow enough ledgers to pass so any of these accounts can be deleted.
        for (int i = 0; i < 300; ++i)
            env.close();

        env(token::setMinter(issuer, minter));
        env.close();

        uint256 const nftId{token::getNextID(env, issuer, 0u, tfTransferable)};
        env(token::mint(minter, 0u),
            token::issuer(issuer),
            txflags(tfTransferable));
        env.close();

        // At the moment issuer and minter cannot delete themselves.
        //  o issuer has an issued NFT in the ledger.
        //  o minter owns an NFT.
        env(acctdelete(issuer, daria), fee(XRP(50)), ter(tecHAS_OBLIGATIONS));
        env(acctdelete(minter, daria), fee(XRP(50)), ter(tecHAS_OBLIGATIONS));
        env.close();

        // Let enough ledgers pass so the account delete transactions are
        // not retried.
        for (int i = 0; i < 15; ++i)
            env.close();

        // becky and carla create offers for minter's NFT.
        env(token::createOffer(becky, nftId, XRP(2)), token::owner(minter));
        env.close();

        uint256 const carlaOfferIndex =
            keylet::nftoffer(carla, env.seq(carla)).key;
        env(token::createOffer(carla, nftId, XRP(3)), token::owner(minter));
        env.close();

        // It should be possible for becky to delete herself, even though
        // becky has an active NFT offer.
        env(acctdelete(becky, daria), fee(XRP(50)));
        env.close();

        // minter accepts carla's offer.
        env(token::acceptBuyOffer(minter, carlaOfferIndex));
        env.close();

        // Now it should be possible for minter to delete themselves since
        // they no longer own an NFT.
        env(acctdelete(minter, daria), fee(XRP(50)));
        env.close();

        // 1. issuer cannot delete themselves because they issued an NFT that
        //    is still in the ledger.
        // 2. carla owns an NFT, so she cannot delete herself.
        env(acctdelete(issuer, daria), fee(XRP(50)), ter(tecHAS_OBLIGATIONS));
        env(acctdelete(carla, daria), fee(XRP(50)), ter(tecHAS_OBLIGATIONS));
        env.close();

        // Let enough ledgers pass so the account delete transactions are
        // not retried.
        for (int i = 0; i < 15; ++i)
            env.close();

        // carla burns her NFT.  Since issuer's NFT is no longer in the
        // ledger, both issuer and carla can delete themselves.
        env(token::burn(carla, nftId));
        env.close();

        env(acctdelete(issuer, daria), fee(XRP(50)));
        env(acctdelete(carla, daria), fee(XRP(50)));
        env.close();
    }

    void
    testNftXxxOffers(FeatureBitset features)
    {
        testcase("nft_buy_offers and nft_sell_offers");

        // The default limit on returned NFToken offers is 250, so we need
        // to produce more than 250 offers of each kind in order to exercise
        // the marker.

        // Fortunately there's nothing in the rules that says an account
        // can't hold more than one offer for the same NFT.  So we only
        // need two accounts to generate the necessary offers.
        using namespace test::jtx;

        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const buyer{"buyer"};

        // A lot of offers requires a lot for reserve.
        env.fund(XRP(1000000), issuer, buyer);
        env.close();

        // Create an NFT that we'll make offers for.
        uint256 const nftID{token::getNextID(env, issuer, 0u, tfTransferable)};
        env(token::mint(issuer, 0), txflags(tfTransferable));
        env.close();

        // A lambda that validates nft_XXX_offers query responses.
        auto checkOffers = [this, &env, &nftID](
                               char const* request,
                               int expectCount,
                               int expectMarkerCount,
                               int line) {
            int markerCount = 0;
            Json::Value allOffers(Json::arrayValue);
            std::string marker;

            // The do/while collects results until no marker is returned.
            do
            {
                Json::Value nftOffers = [&env, &nftID, &request, &marker]() {
                    Json::Value params;
                    params[jss::nft_id] = to_string(nftID);

                    if (!marker.empty())
                        params[jss::marker] = marker;
                    return env.rpc("json", request, to_string(params));
                }();

                // If there are no offers for the NFT we get an error
                if (expectCount == 0)
                {
                    if (expect(
                            nftOffers.isMember(jss::result),
                            "expected \"result\"",
                            __FILE__,
                            line))
                    {
                        if (expect(
                                nftOffers[jss::result].isMember(jss::error),
                                "expected \"error\"",
                                __FILE__,
                                line))
                        {
                            expect(
                                nftOffers[jss::result][jss::error].asString() ==
                                    "objectNotFound",
                                "expected \"objectNotFound\"",
                                __FILE__,
                                line);
                        }
                    }
                    break;
                }

                marker.clear();
                if (expect(
                        nftOffers.isMember(jss::result),
                        "expected \"result\"",
                        __FILE__,
                        line))
                {
                    Json::Value& result = nftOffers[jss::result];

                    if (result.isMember(jss::marker))
                    {
                        ++markerCount;
                        marker = result[jss::marker].asString();
                    }

                    if (expect(
                            result.isMember(jss::offers),
                            "expected \"offers\"",
                            __FILE__,
                            line))
                    {
                        Json::Value& someOffers = result[jss::offers];
                        for (std::size_t i = 0; i < someOffers.size(); ++i)
                            allOffers.append(someOffers[i]);
                    }
                }
            } while (!marker.empty());

            // Verify the contents of allOffers makes sense.
            expect(
                allOffers.size() == expectCount,
                "Unexpected returned offer count",
                __FILE__,
                line);
            expect(
                markerCount == expectMarkerCount,
                "Unexpected marker count",
                __FILE__,
                line);
            std::optional<int> globalFlags;
            std::set<std::string> offerIndexes;
            std::set<std::string> amounts;
            for (Json::Value const& offer : allOffers)
            {
                // The flags on all found offers should be the same.
                if (!globalFlags)
                    globalFlags = offer[jss::flags].asInt();

                expect(
                    *globalFlags == offer[jss::flags].asInt(),
                    "Inconsistent flags returned",
                    __FILE__,
                    line);

                // The test conditions should produce unique indexes and
                // amounts for all offers.
                offerIndexes.insert(offer[jss::nft_offer_index].asString());
                amounts.insert(offer[jss::amount].asString());
            }

            expect(
                offerIndexes.size() == expectCount,
                "Duplicate indexes returned?",
                __FILE__,
                line);
            expect(
                amounts.size() == expectCount,
                "Duplicate amounts returned?",
                __FILE__,
                line);
        };

        // There are no sell offers.
        checkOffers("nft_sell_offers", 0, false, __LINE__);

        // A lambda that generates sell offers.
        STAmount sellPrice = XRP(0);
        auto makeSellOffers =
            [&env, &issuer, &nftID, &sellPrice](STAmount const& limit) {
                // Save a little test time by not closing too often.
                int offerCount = 0;
                while (sellPrice < limit)
                {
                    sellPrice += XRP(1);
                    env(token::createOffer(issuer, nftID, sellPrice),
                        txflags(tfSellNFToken));
                    if (++offerCount % 10 == 0)
                        env.close();
                }
                env.close();
            };

        // There is one sell offer.
        makeSellOffers(XRP(1));
        checkOffers("nft_sell_offers", 1, 0, __LINE__);

        // There are 250 sell offers.
        makeSellOffers(XRP(250));
        checkOffers("nft_sell_offers", 250, 0, __LINE__);

        // There are 251 sell offers.
        makeSellOffers(XRP(251));
        checkOffers("nft_sell_offers", 251, 1, __LINE__);

        // There are 500 sell offers.
        makeSellOffers(XRP(500));
        checkOffers("nft_sell_offers", 500, 1, __LINE__);

        // There are 501 sell offers.
        makeSellOffers(XRP(501));
        checkOffers("nft_sell_offers", 501, 2, __LINE__);

        // There are no buy offers.
        checkOffers("nft_buy_offers", 0, 0, __LINE__);

        // A lambda that generates buy offers.
        STAmount buyPrice = XRP(0);
        auto makeBuyOffers =
            [&env, &buyer, &issuer, &nftID, &buyPrice](STAmount const& limit) {
                // Save a little test time by not closing too often.
                int offerCount = 0;
                while (buyPrice < limit)
                {
                    buyPrice += XRP(1);
                    env(token::createOffer(buyer, nftID, buyPrice),
                        token::owner(issuer));
                    if (++offerCount % 10 == 0)
                        env.close();
                }
                env.close();
            };

        // There is one buy offer;
        makeBuyOffers(XRP(1));
        checkOffers("nft_buy_offers", 1, 0, __LINE__);

        // There are 250 buy offers.
        makeBuyOffers(XRP(250));
        checkOffers("nft_buy_offers", 250, 0, __LINE__);

        // There are 251 buy offers.
        makeBuyOffers(XRP(251));
        checkOffers("nft_buy_offers", 251, 1, __LINE__);

        // There are 500 buy offers.
        makeBuyOffers(XRP(500));
        checkOffers("nft_buy_offers", 500, 1, __LINE__);

        // There are 501 buy offers.
        makeBuyOffers(XRP(501));
        checkOffers("nft_buy_offers", 501, 2, __LINE__);
    }

    void
    testFixNFTokenNegOffer(FeatureBitset features)
    {
        // Exercise changes introduced by fixNFTokenNegOffer.
        using namespace test::jtx;

        testcase("fixNFTokenNegOffer");

        Account const issuer{"issuer"};
        Account const buyer{"buyer"};
        Account const gw{"gw"};
        IOU const gwXAU(gw["XAU"]);

        // Test both with and without fixNFTokenNegOffer and
        // fixNonFungibleTokensV1_2. Need to turn off fixNonFungibleTokensV1_2
        // as well because that amendment came later and addressed the
        // acceptance side of this issue.
        for (auto const& tweakedFeatures :
             {features - fixNFTokenNegOffer - featureNonFungibleTokensV1_1 -
                  fixNonFungibleTokensV1_2,
              features - fixNFTokenNegOffer - featureNonFungibleTokensV1_1,
              features | fixNFTokenNegOffer})
        {
            // There was a bug in the initial NFT implementation that
            // allowed offers to be placed with negative amounts.  Verify
            // that fixNFTokenNegOffer addresses the problem.
            Env env{*this, tweakedFeatures};

            env.fund(XRP(1000000), issuer, buyer, gw);
            env.close();

            env(trust(issuer, gwXAU(2000)));
            env(trust(buyer, gwXAU(2000)));
            env.close();

            env(pay(gw, issuer, gwXAU(1000)));
            env(pay(gw, buyer, gwXAU(1000)));
            env.close();

            // Create an NFT that we'll make XRP offers for.
            uint256 const nftID0{
                token::getNextID(env, issuer, 0u, tfTransferable)};
            env(token::mint(issuer, 0), txflags(tfTransferable));
            env.close();

            // Create an NFT that we'll make IOU offers for.
            uint256 const nftID1{
                token::getNextID(env, issuer, 1u, tfTransferable)};
            env(token::mint(issuer, 1), txflags(tfTransferable));
            env.close();

            TER const offerCreateTER = tweakedFeatures[fixNFTokenNegOffer]
                ? static_cast<TER>(temBAD_AMOUNT)
                : static_cast<TER>(tesSUCCESS);

            // Make offers with negative amounts for the NFTs
            uint256 const sellNegXrpOfferIndex =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftID0, XRP(-2)),
                txflags(tfSellNFToken),
                ter(offerCreateTER));
            env.close();

            uint256 const sellNegIouOfferIndex =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftID1, gwXAU(-2)),
                txflags(tfSellNFToken),
                ter(offerCreateTER));
            env.close();

            uint256 const buyNegXrpOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftID0, XRP(-1)),
                token::owner(issuer),
                ter(offerCreateTER));
            env.close();

            uint256 const buyNegIouOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftID1, gwXAU(-1)),
                token::owner(issuer),
                ter(offerCreateTER));
            env.close();

            {
                // Now try to accept the offers.
                //  1. If fixNFTokenNegOffer is NOT enabled get tecINTERNAL.
                //  2. If fixNFTokenNegOffer IS enabled get tecOBJECT_NOT_FOUND.
                TER const offerAcceptTER = tweakedFeatures[fixNFTokenNegOffer]
                    ? static_cast<TER>(tecOBJECT_NOT_FOUND)
                    : static_cast<TER>(tecINTERNAL);

                // Sell offers.
                env(token::acceptSellOffer(buyer, sellNegXrpOfferIndex),
                    ter(offerAcceptTER));
                env.close();
                env(token::acceptSellOffer(buyer, sellNegIouOfferIndex),
                    ter(offerAcceptTER));
                env.close();

                // Buy offers.
                env(token::acceptBuyOffer(issuer, buyNegXrpOfferIndex),
                    ter(offerAcceptTER));
                env.close();
                env(token::acceptBuyOffer(issuer, buyNegIouOfferIndex),
                    ter(offerAcceptTER));
                env.close();
            }
            {
                //  1. If fixNFTokenNegOffer is enabled get tecOBJECT_NOT_FOUND
                //  2. If it is not enabled, but fixNonFungibleTokensV1_2 is
                //  enabled, get tecOBJECT_NOT_FOUND.
                //  3. If neither are enabled, get tesSUCCESS.
                TER const offerAcceptTER = tweakedFeatures[fixNFTokenNegOffer]
                    ? static_cast<TER>(tecOBJECT_NOT_FOUND)
                    : static_cast<TER>(tesSUCCESS);

                // Brokered offers.
                env(token::brokerOffers(
                        gw, buyNegXrpOfferIndex, sellNegXrpOfferIndex),
                    ter(offerAcceptTER));
                env.close();
                env(token::brokerOffers(
                        gw, buyNegIouOfferIndex, sellNegIouOfferIndex),
                    ter(offerAcceptTER));
                env.close();
            }
        }

        // Test what happens if NFTokenOffers are created with negative amounts
        // and then fixNFTokenNegOffer goes live.  What does an acceptOffer do?
        {
            Env env{
                *this,
                features - fixNFTokenNegOffer - featureNonFungibleTokensV1_1};

            env.fund(XRP(1000000), issuer, buyer, gw);
            env.close();

            env(trust(issuer, gwXAU(2000)));
            env(trust(buyer, gwXAU(2000)));
            env.close();

            env(pay(gw, issuer, gwXAU(1000)));
            env(pay(gw, buyer, gwXAU(1000)));
            env.close();

            // Create an NFT that we'll make XRP offers for.
            uint256 const nftID0{
                token::getNextID(env, issuer, 0u, tfTransferable)};
            env(token::mint(issuer, 0), txflags(tfTransferable));
            env.close();

            // Create an NFT that we'll make IOU offers for.
            uint256 const nftID1{
                token::getNextID(env, issuer, 1u, tfTransferable)};
            env(token::mint(issuer, 1), txflags(tfTransferable));
            env.close();

            // Make offers with negative amounts for the NFTs
            uint256 const sellNegXrpOfferIndex =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftID0, XRP(-2)),
                txflags(tfSellNFToken));
            env.close();

            uint256 const sellNegIouOfferIndex =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftID1, gwXAU(-2)),
                txflags(tfSellNFToken));
            env.close();

            uint256 const buyNegXrpOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftID0, XRP(-1)),
                token::owner(issuer));
            env.close();

            uint256 const buyNegIouOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(buyer, nftID1, gwXAU(-1)),
                token::owner(issuer));
            env.close();

            // Now the amendment passes.
            env.enableFeature(fixNFTokenNegOffer);
            env.close();

            // All attempts to accept the offers with negative amounts
            // should fail with temBAD_OFFER.
            env(token::acceptSellOffer(buyer, sellNegXrpOfferIndex),
                ter(temBAD_OFFER));
            env.close();
            env(token::acceptSellOffer(buyer, sellNegIouOfferIndex),
                ter(temBAD_OFFER));
            env.close();

            // Buy offers.
            env(token::acceptBuyOffer(issuer, buyNegXrpOfferIndex),
                ter(temBAD_OFFER));
            env.close();
            env(token::acceptBuyOffer(issuer, buyNegIouOfferIndex),
                ter(temBAD_OFFER));
            env.close();

            // Brokered offers.
            env(token::brokerOffers(
                    gw, buyNegXrpOfferIndex, sellNegXrpOfferIndex),
                ter(temBAD_OFFER));
            env.close();
            env(token::brokerOffers(
                    gw, buyNegIouOfferIndex, sellNegIouOfferIndex),
                ter(temBAD_OFFER));
            env.close();
        }

        // Test buy offers with a destination with and without
        // fixNFTokenNegOffer.
        for (auto const& tweakedFeatures :
             {features - fixNFTokenNegOffer - featureNonFungibleTokensV1_1,
              features | fixNFTokenNegOffer})
        {
            Env env{*this, tweakedFeatures};

            env.fund(XRP(1000000), issuer, buyer);

            // Create an NFT that we'll make offers for.
            uint256 const nftID{
                token::getNextID(env, issuer, 0u, tfTransferable)};
            env(token::mint(issuer, 0), txflags(tfTransferable));
            env.close();

            TER const offerCreateTER = tweakedFeatures[fixNFTokenNegOffer]
                ? static_cast<TER>(tesSUCCESS)
                : static_cast<TER>(temMALFORMED);

            env(token::createOffer(buyer, nftID, drops(1)),
                token::owner(issuer),
                token::destination(issuer),
                ter(offerCreateTER));
            env.close();
        }
    }

    void
    testIOUWithTransferFee(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("Payments with IOU transfer fees");

        for (auto const& tweakedFeatures :
             {features - fixNonFungibleTokensV1_2,
              features | fixNonFungibleTokensV1_2})
        {
            Env env{*this, tweakedFeatures};

            Account const minter{"minter"};
            Account const secondarySeller{"seller"};
            Account const buyer{"buyer"};
            Account const gw{"gateway"};
            Account const broker{"broker"};
            IOU const gwXAU(gw["XAU"]);
            IOU const gwXPB(gw["XPB"]);

            env.fund(XRP(1000), gw, minter, secondarySeller, buyer, broker);
            env.close();

            env(trust(minter, gwXAU(2000)));
            env(trust(secondarySeller, gwXAU(2000)));
            env(trust(broker, gwXAU(10000)));
            env(trust(buyer, gwXAU(2000)));
            env(trust(buyer, gwXPB(2000)));
            env.close();

            // The IOU issuer has a 2% transfer rate
            env(rate(gw, 1.02));
            env.close();

            auto expectInitialState = [this,
                                       &env,
                                       &buyer,
                                       &minter,
                                       &secondarySeller,
                                       &broker,
                                       &gw,
                                       &gwXAU,
                                       &gwXPB]() {
                // Buyer should have XAU 1000, XPB 0
                // Minter should have XAU 0, XPB 0
                // Secondary seller should have XAU 0, XPB 0
                // Broker should have XAU 5000, XPB 0
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(1000));
                BEAST_EXPECT(env.balance(buyer, gwXPB) == gwXPB(0));
                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(minter, gwXPB) == gwXPB(0));
                BEAST_EXPECT(env.balance(secondarySeller, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(secondarySeller, gwXPB) == gwXPB(0));
                BEAST_EXPECT(env.balance(broker, gwXAU) == gwXAU(5000));
                BEAST_EXPECT(env.balance(broker, gwXPB) == gwXPB(0));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(-1000));
                BEAST_EXPECT(env.balance(gw, buyer["XPB"]) == gwXPB(0));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(0));
                BEAST_EXPECT(env.balance(gw, minter["XPB"]) == gwXPB(0));
                BEAST_EXPECT(
                    env.balance(gw, secondarySeller["XAU"]) == gwXAU(0));
                BEAST_EXPECT(
                    env.balance(gw, secondarySeller["XPB"]) == gwXPB(0));
                BEAST_EXPECT(env.balance(gw, broker["XAU"]) == gwXAU(-5000));
                BEAST_EXPECT(env.balance(gw, broker["XPB"]) == gwXPB(0));
            };

            auto reinitializeTrustLineBalances = [&expectInitialState,
                                                  &env,
                                                  &buyer,
                                                  &minter,
                                                  &secondarySeller,
                                                  &broker,
                                                  &gw,
                                                  &gwXAU,
                                                  &gwXPB]() {
                if (auto const difference =
                        gwXAU(1000) - env.balance(buyer, gwXAU);
                    difference > gwXAU(0))
                    env(pay(gw, buyer, difference));
                if (env.balance(buyer, gwXPB) > gwXPB(0))
                    env(pay(buyer, gw, env.balance(buyer, gwXPB)));
                if (env.balance(minter, gwXAU) > gwXAU(0))
                    env(pay(minter, gw, env.balance(minter, gwXAU)));
                if (env.balance(minter, gwXPB) > gwXPB(0))
                    env(pay(minter, gw, env.balance(minter, gwXPB)));
                if (env.balance(secondarySeller, gwXAU) > gwXAU(0))
                    env(
                        pay(secondarySeller,
                            gw,
                            env.balance(secondarySeller, gwXAU)));
                if (env.balance(secondarySeller, gwXPB) > gwXPB(0))
                    env(
                        pay(secondarySeller,
                            gw,
                            env.balance(secondarySeller, gwXPB)));
                auto brokerDiff = gwXAU(5000) - env.balance(broker, gwXAU);
                if (brokerDiff > gwXAU(0))
                    env(pay(gw, broker, brokerDiff));
                else if (brokerDiff < gwXAU(0))
                {
                    brokerDiff.negate();
                    env(pay(broker, gw, brokerDiff));
                }
                if (env.balance(broker, gwXPB) > gwXPB(0))
                    env(pay(broker, gw, env.balance(broker, gwXPB)));
                env.close();
                expectInitialState();
            };

            auto mintNFT = [&env](Account const& minter, int transferFee = 0) {
                uint256 const nftID = token::getNextID(
                    env, minter, 0, tfTransferable, transferFee);
                env(token::mint(minter),
                    token::xferFee(transferFee),
                    txflags(tfTransferable));
                env.close();
                return nftID;
            };

            auto createBuyOffer =
                [&env](
                    Account const& offerer,
                    Account const& owner,
                    uint256 const& nftID,
                    STAmount const& amount,
                    std::optional<TER const> const terCode = {}) {
                    uint256 const offerID =
                        keylet::nftoffer(offerer, env.seq(offerer)).key;
                    env(token::createOffer(offerer, nftID, amount),
                        token::owner(owner),
                        terCode ? ter(*terCode)
                                : ter(static_cast<TER>(tesSUCCESS)));
                    env.close();
                    return offerID;
                };

            auto createSellOffer =
                [&env](
                    Account const& offerer,
                    uint256 const& nftID,
                    STAmount const& amount,
                    std::optional<TER const> const terCode = {}) {
                    uint256 const offerID =
                        keylet::nftoffer(offerer, env.seq(offerer)).key;
                    env(token::createOffer(offerer, nftID, amount),
                        txflags(tfSellNFToken),
                        terCode ? ter(*terCode)
                                : ter(static_cast<TER>(tesSUCCESS)));
                    env.close();
                    return offerID;
                };

            {
                // Buyer attempts to send 100% of their balance of an IOU
                // (sellside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createSellOffer(minter, nftID, gwXAU(1000));
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tecINSUFFICIENT_FUNDS)
                    : static_cast<TER>(tesSUCCESS);
                env(token::acceptSellOffer(buyer, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                    expectInitialState();
                else
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));
                    BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(-20));
                    BEAST_EXPECT(
                        env.balance(gw, minter["XAU"]) == gwXAU(-1000));
                    BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(20));
                }
            }
            {
                // Buyer attempts to send 100% of their balance of an IOU
                // (buyside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createBuyOffer(buyer, minter, nftID, gwXAU(1000));
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tecINSUFFICIENT_FUNDS)
                    : static_cast<TER>(tesSUCCESS);
                env(token::acceptBuyOffer(minter, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                    expectInitialState();
                else
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));
                    BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(-20));
                    BEAST_EXPECT(
                        env.balance(gw, minter["XAU"]) == gwXAU(-1000));
                    BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(20));
                }
            }
            {
                // Buyer attempts to send an amount less than 100% of their
                // balance of an IOU, but such that the addition of the transfer
                // fee would be greater than the buyer's balance (sellside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID = createSellOffer(minter, nftID, gwXAU(995));
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tecINSUFFICIENT_FUNDS)
                    : static_cast<TER>(tesSUCCESS);
                env(token::acceptSellOffer(buyer, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                    expectInitialState();
                else
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(995));
                    BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(-14.9));
                    BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-995));
                    BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(14.9));
                }
            }
            {
                // Buyer attempts to send an amount less than 100% of their
                // balance of an IOU, but such that the addition of the transfer
                // fee would be greater than the buyer's balance (buyside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createBuyOffer(buyer, minter, nftID, gwXAU(995));
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tecINSUFFICIENT_FUNDS)
                    : static_cast<TER>(tesSUCCESS);
                env(token::acceptBuyOffer(minter, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                    expectInitialState();
                else
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(995));
                    BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(-14.9));
                    BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-995));
                    BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(14.9));
                }
            }
            {
                // Buyer attempts to send an amount less than 100% of their
                // balance of an IOU with a transfer fee, and such that the
                // addition of the transfer fee is still less than their balance
                // (sellside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID = createSellOffer(minter, nftID, gwXAU(900));
                env(token::acceptSellOffer(buyer, offerID));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(900));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(82));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-900));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(-82));
            }
            {
                // Buyer attempts to send an amount less than 100% of their
                // balance of an IOU with a transfer fee, and such that the
                // addition of the transfer fee is still less than their balance
                // (buyside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createBuyOffer(buyer, minter, nftID, gwXAU(900));
                env(token::acceptBuyOffer(minter, offerID));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(900));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(82));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-900));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(-82));
            }
            {
                // Buyer attempts to send an amount less than 100% of their
                // balance of an IOU with a transfer fee, and such that the
                // addition of the transfer fee is equal than their balance
                // (sellside)
                reinitializeTrustLineBalances();

                // pay them an additional XAU 20 to cover transfer rate
                env(pay(gw, buyer, gwXAU(20)));
                env.close();

                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createSellOffer(minter, nftID, gwXAU(1000));
                env(token::acceptSellOffer(buyer, offerID));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-1000));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(0));
            }
            {
                // Buyer attempts to send an amount less than 100% of their
                // balance of an IOU with a transfer fee, and such that the
                // addition of the transfer fee is equal than their balance
                // (buyside)
                reinitializeTrustLineBalances();

                // pay them an additional XAU 20 to cover transfer rate
                env(pay(gw, buyer, gwXAU(20)));
                env.close();

                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createBuyOffer(buyer, minter, nftID, gwXAU(1000));
                env(token::acceptBuyOffer(minter, offerID));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-1000));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(0));
            }
            {
                // Gateway attempts to buy NFT with their own IOU - no
                // transfer fee is calculated here (sellside)
                reinitializeTrustLineBalances();

                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createSellOffer(minter, nftID, gwXAU(1000));
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tesSUCCESS)
                    : static_cast<TER>(tecINSUFFICIENT_FUNDS);
                env(token::acceptSellOffer(gw, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));
                    BEAST_EXPECT(
                        env.balance(gw, minter["XAU"]) == gwXAU(-1000));
                }
                else
                    expectInitialState();
            }
            {
                // Gateway attempts to buy NFT with their own IOU - no
                // transfer fee is calculated here (buyside)
                reinitializeTrustLineBalances();

                auto const nftID = mintNFT(minter);
                auto const offerTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tesSUCCESS)
                    : static_cast<TER>(tecUNFUNDED_OFFER);
                auto const offerID =
                    createBuyOffer(gw, minter, nftID, gwXAU(1000), {offerTER});
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tesSUCCESS)
                    : static_cast<TER>(tecOBJECT_NOT_FOUND);
                env(token::acceptBuyOffer(minter, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(1000));
                    BEAST_EXPECT(
                        env.balance(gw, minter["XAU"]) == gwXAU(-1000));
                }
                else
                    expectInitialState();
            }
            {
                // Gateway attempts to buy NFT with their own IOU for more
                // than minter trusts (sellside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createSellOffer(minter, nftID, gwXAU(5000));
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tesSUCCESS)
                    : static_cast<TER>(tecINSUFFICIENT_FUNDS);
                env(token::acceptSellOffer(gw, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(5000));
                    BEAST_EXPECT(
                        env.balance(gw, minter["XAU"]) == gwXAU(-5000));
                }
                else
                    expectInitialState();
            }
            {
                // Gateway attempts to buy NFT with their own IOU for more
                // than minter trusts (buyside)
                reinitializeTrustLineBalances();

                auto const nftID = mintNFT(minter);
                auto const offerTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tesSUCCESS)
                    : static_cast<TER>(tecUNFUNDED_OFFER);
                auto const offerID =
                    createBuyOffer(gw, minter, nftID, gwXAU(5000), {offerTER});
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tesSUCCESS)
                    : static_cast<TER>(tecOBJECT_NOT_FOUND);
                env(token::acceptBuyOffer(minter, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(5000));
                    BEAST_EXPECT(
                        env.balance(gw, minter["XAU"]) == gwXAU(-5000));
                }
                else
                    expectInitialState();
            }
            {
                // Gateway is the NFT minter and attempts to sell NFT for an
                // amount that would be greater than a balance if there were a
                // transfer fee calculated in this transaction. (sellside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(gw);
                auto const offerID = createSellOffer(gw, nftID, gwXAU(1000));
                env(token::acceptSellOffer(buyer, offerID));
                env.close();

                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(0));
            }
            {
                // Gateway is the NFT minter and attempts to sell NFT for an
                // amount that would be greater than a balance if there were a
                // transfer fee calculated in this transaction. (buyside)
                reinitializeTrustLineBalances();

                auto const nftID = mintNFT(gw);
                auto const offerID =
                    createBuyOffer(buyer, gw, nftID, gwXAU(1000));
                env(token::acceptBuyOffer(gw, offerID));
                env.close();

                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(0));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(0));
            }
            {
                // Gateway is the NFT minter and attempts to sell NFT for an
                // amount that is greater than a balance before transfer fees.
                // (sellside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(gw);
                auto const offerID = createSellOffer(gw, nftID, gwXAU(2000));
                env(token::acceptSellOffer(buyer, offerID),
                    ter(static_cast<TER>(tecINSUFFICIENT_FUNDS)));
                env.close();
                expectInitialState();
            }
            {
                // Gateway is the NFT minter and attempts to sell NFT for an
                // amount that is greater than a balance before transfer fees.
                // (buyside)
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(gw);
                auto const offerID =
                    createBuyOffer(buyer, gw, nftID, gwXAU(2000));
                env(token::acceptBuyOffer(gw, offerID),
                    ter(static_cast<TER>(tecINSUFFICIENT_FUNDS)));
                env.close();
                expectInitialState();
            }
            {
                // Minter attempts to sell the token for XPB 10, which they
                // have no trust line for and buyer has none of (sellside).
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID = createSellOffer(minter, nftID, gwXPB(10));
                env(token::acceptSellOffer(buyer, offerID),
                    ter(static_cast<TER>(tecINSUFFICIENT_FUNDS)));
                env.close();
                expectInitialState();
            }
            {
                // Minter attempts to sell the token for XPB 10, which they
                // have no trust line for and buyer has none of (buyside).
                reinitializeTrustLineBalances();
                auto const nftID = mintNFT(minter);
                auto const offerID = createBuyOffer(
                    buyer,
                    minter,
                    nftID,
                    gwXPB(10),
                    {static_cast<TER>(tecUNFUNDED_OFFER)});
                env(token::acceptBuyOffer(minter, offerID),
                    ter(static_cast<TER>(tecOBJECT_NOT_FOUND)));
                env.close();
                expectInitialState();
            }
            {
                // Minter attempts to sell the token for XPB 10 and the buyer
                // has it but the minter has no trust line. Trust line is
                // created as a result of the tx (sellside).
                reinitializeTrustLineBalances();
                env(pay(gw, buyer, gwXPB(100)));
                env.close();

                auto const nftID = mintNFT(minter);
                auto const offerID = createSellOffer(minter, nftID, gwXPB(10));
                env(token::acceptSellOffer(buyer, offerID));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXPB) == gwXPB(10));
                BEAST_EXPECT(env.balance(buyer, gwXPB) == gwXPB(89.8));
                BEAST_EXPECT(env.balance(gw, minter["XPB"]) == gwXPB(-10));
                BEAST_EXPECT(env.balance(gw, buyer["XPB"]) == gwXPB(-89.8));
            }
            {
                // Minter attempts to sell the token for XPB 10 and the buyer
                // has it but the minter has no trust line. Trust line is
                // created as a result of the tx (buyside).
                reinitializeTrustLineBalances();
                env(pay(gw, buyer, gwXPB(100)));
                env.close();

                auto const nftID = mintNFT(minter);
                auto const offerID =
                    createBuyOffer(buyer, minter, nftID, gwXPB(10));
                env(token::acceptBuyOffer(minter, offerID));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXPB) == gwXPB(10));
                BEAST_EXPECT(env.balance(buyer, gwXPB) == gwXPB(89.8));
                BEAST_EXPECT(env.balance(gw, minter["XPB"]) == gwXPB(-10));
                BEAST_EXPECT(env.balance(gw, buyer["XPB"]) == gwXPB(-89.8));
            }
            {
                // There is a transfer fee on the NFT and buyer has exact
                // amount (sellside)
                reinitializeTrustLineBalances();

                // secondarySeller has to sell it because transfer fees only
                // happen on secondary sales
                auto const nftID = mintNFT(minter, 3000);  // 3%
                auto const primaryOfferID =
                    createSellOffer(minter, nftID, XRP(0));
                env(token::acceptSellOffer(secondarySeller, primaryOfferID));
                env.close();

                // now we can do a secondary sale
                auto const offerID =
                    createSellOffer(secondarySeller, nftID, gwXAU(1000));
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tecINSUFFICIENT_FUNDS)
                    : static_cast<TER>(tesSUCCESS);
                env(token::acceptSellOffer(buyer, offerID), ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                    expectInitialState();
                else
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(30));
                    BEAST_EXPECT(
                        env.balance(secondarySeller, gwXAU) == gwXAU(970));
                    BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(-20));
                    BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-30));
                    BEAST_EXPECT(
                        env.balance(gw, secondarySeller["XAU"]) == gwXAU(-970));
                    BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(20));
                }
            }
            {
                // There is a transfer fee on the NFT and buyer has exact
                // amount (buyside)
                reinitializeTrustLineBalances();

                // secondarySeller has to sell it because transfer fees only
                // happen on secondary sales
                auto const nftID = mintNFT(minter, 3000);  // 3%
                auto const primaryOfferID =
                    createSellOffer(minter, nftID, XRP(0));
                env(token::acceptSellOffer(secondarySeller, primaryOfferID));
                env.close();

                // now we can do a secondary sale
                auto const offerID =
                    createBuyOffer(buyer, secondarySeller, nftID, gwXAU(1000));
                auto const sellTER = tweakedFeatures[fixNonFungibleTokensV1_2]
                    ? static_cast<TER>(tecINSUFFICIENT_FUNDS)
                    : static_cast<TER>(tesSUCCESS);
                env(token::acceptBuyOffer(secondarySeller, offerID),
                    ter(sellTER));
                env.close();

                if (tweakedFeatures[fixNonFungibleTokensV1_2])
                    expectInitialState();
                else
                {
                    BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(30));
                    BEAST_EXPECT(
                        env.balance(secondarySeller, gwXAU) == gwXAU(970));
                    BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(-20));
                    BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-30));
                    BEAST_EXPECT(
                        env.balance(gw, secondarySeller["XAU"]) == gwXAU(-970));
                    BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(20));
                }
            }
            {
                // There is a transfer fee on the NFT and buyer has enough
                // (sellside)
                reinitializeTrustLineBalances();

                // secondarySeller has to sell it because transfer fees only
                // happen on secondary sales
                auto const nftID = mintNFT(minter, 3000);  // 3%
                auto const primaryOfferID =
                    createSellOffer(minter, nftID, XRP(0));
                env(token::acceptSellOffer(secondarySeller, primaryOfferID));
                env.close();

                // now we can do a secondary sale
                auto const offerID =
                    createSellOffer(secondarySeller, nftID, gwXAU(900));
                env(token::acceptSellOffer(buyer, offerID));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(27));
                BEAST_EXPECT(env.balance(secondarySeller, gwXAU) == gwXAU(873));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(82));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-27));
                BEAST_EXPECT(
                    env.balance(gw, secondarySeller["XAU"]) == gwXAU(-873));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(-82));
            }
            {
                // There is a transfer fee on the NFT and buyer has enough
                // (buyside)
                reinitializeTrustLineBalances();

                // secondarySeller has to sell it because transfer fees only
                // happen on secondary sales
                auto const nftID = mintNFT(minter, 3000);  // 3%
                auto const primaryOfferID =
                    createSellOffer(minter, nftID, XRP(0));
                env(token::acceptSellOffer(secondarySeller, primaryOfferID));
                env.close();

                // now we can do a secondary sale
                auto const offerID =
                    createBuyOffer(buyer, secondarySeller, nftID, gwXAU(900));
                env(token::acceptBuyOffer(secondarySeller, offerID));
                env.close();

                // receives 3% of 900 - 27
                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(27));
                // receives 97% of 900 - 873
                BEAST_EXPECT(env.balance(secondarySeller, gwXAU) == gwXAU(873));
                // pays 900 plus 2% transfer fee on XAU - 918
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(82));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-27));
                BEAST_EXPECT(
                    env.balance(gw, secondarySeller["XAU"]) == gwXAU(-873));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(-82));
            }
            {
                // There is a broker fee on the NFT. XAU transfer fee is only
                // calculated from the buyer's output, not deducted from
                // broker fee.
                //
                // For a payment of 500 with a 2% IOU transfee fee and 100
                // broker fee:
                //
                // A) Total sale amount + IOU transfer fee is paid by buyer
                //      (Buyer pays (1.02 * 500) = 510)
                // B) GW receives the additional IOU transfer fee
                //      (GW receives 10 from buyer calculated above)
                // C) Broker receives broker fee (no IOU transfer fee)
                //      (Broker receives 100 from buyer)
                // D) Seller receives balance (no IOU transfer fee)
                //      (Seller receives (510 - 10 - 100) = 400)
                reinitializeTrustLineBalances();

                auto const nftID = mintNFT(minter);
                auto const sellOffer =
                    createSellOffer(minter, nftID, gwXAU(300));
                auto const buyOffer =
                    createBuyOffer(buyer, minter, nftID, gwXAU(500));
                env(token::brokerOffers(broker, buyOffer, sellOffer),
                    token::brokerFee(gwXAU(100)));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(400));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(490));
                BEAST_EXPECT(env.balance(broker, gwXAU) == gwXAU(5100));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-400));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(-490));
                BEAST_EXPECT(env.balance(gw, broker["XAU"]) == gwXAU(-5100));
            }
            {
                // There is broker and transfer fee on the NFT
                //
                // For a payment of 500 with a 2% IOU transfer fee, 3% NFT
                // transfer fee, and 100 broker fee:
                //
                // A) Total sale amount + IOU transfer fee is paid by buyer
                //      (Buyer pays (1.02 * 500) = 510)
                // B) GW receives the additional IOU transfer fee
                //      (GW receives 10 from buyer calculated above)
                // C) Broker receives broker fee (no IOU transfer fee)
                //      (Broker receives 100 from buyer)
                // D) Minter receives transfer fee (no IOU transfer fee)
                //      (Minter receives 0.03 * (510 - 10 - 100) = 12)
                // E) Seller receives balance (no IOU transfer fee)
                //      (Seller receives (510 - 10 - 100 - 12) = 388)
                reinitializeTrustLineBalances();

                // secondarySeller has to sell it because transfer fees only
                // happen on secondary sales
                auto const nftID = mintNFT(minter, 3000);  // 3%
                auto const primaryOfferID =
                    createSellOffer(minter, nftID, XRP(0));
                env(token::acceptSellOffer(secondarySeller, primaryOfferID));
                env.close();

                // now we can do a secondary sale
                auto const sellOffer =
                    createSellOffer(secondarySeller, nftID, gwXAU(300));
                auto const buyOffer =
                    createBuyOffer(buyer, secondarySeller, nftID, gwXAU(500));
                env(token::brokerOffers(broker, buyOffer, sellOffer),
                    token::brokerFee(gwXAU(100)));
                env.close();

                BEAST_EXPECT(env.balance(minter, gwXAU) == gwXAU(12));
                BEAST_EXPECT(env.balance(buyer, gwXAU) == gwXAU(490));
                BEAST_EXPECT(env.balance(secondarySeller, gwXAU) == gwXAU(388));
                BEAST_EXPECT(env.balance(broker, gwXAU) == gwXAU(5100));
                BEAST_EXPECT(env.balance(gw, minter["XAU"]) == gwXAU(-12));
                BEAST_EXPECT(env.balance(gw, buyer["XAU"]) == gwXAU(-490));
                BEAST_EXPECT(
                    env.balance(gw, secondarySeller["XAU"]) == gwXAU(-388));
                BEAST_EXPECT(env.balance(gw, broker["XAU"]) == gwXAU(-5100));
            }
        }
    }

    void
    testBrokeredSaleToSelf(FeatureBitset features)
    {
        // There was a bug that if an account had...
        //
        //  1. An NFToken, and
        //  2. An offer on the ledger to buy that same token, and
        //  3. Also an offer of the ledger to sell that same token,
        //
        // Then someone could broker the two offers.  This would result in
        // the NFToken being bought and returned to the original owner and
        // the broker pocketing the profit.
        //
        // This unit test verifies that the fixNonFungibleTokensV1_2 amendment
        // fixes that bug.
        testcase("Brokered sale to self");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const broker{"broker"};

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;
        env.fund(XRP(10000), alice, bob, broker);
        env.close();

        // For this scenario to occur we need the following steps:
        //
        //  1. alice mints NFT.
        //  2. bob creates a buy offer for it for 5 XRP.
        //  3. alice decides to gift the NFT to bob for 0.
        //     creating a sell offer (hopefully using a destination too)
        //  4. Bob accepts the sell offer, because it is better than
        //     paying 5 XRP.
        //  5. At this point, bob has the NFT and still has their buy
        //     offer from when they did not have the NFT!  This is because
        //     the order book is not cleared when an NFT changes hands.
        //  6. Now that Bob owns the NFT, he cannot create new buy offers.
        //     However he still has one left over from when he did not own
        //     it. He can create new sell offers and does.
        //  7. Now that bob has both a buy and a sell offer for the same NFT,
        //     a broker can sell the NFT that bob owns to bob and pocket the
        //     difference.
        uint256 const nftId{token::getNextID(env, alice, 0u, tfTransferable)};
        env(token::mint(alice, 0u), txflags(tfTransferable));
        env.close();

        // Bob creates a buy offer for 5 XRP.  Alice creates a sell offer
        // for 0 XRP.
        uint256 const bobBuyOfferIndex =
            keylet::nftoffer(bob, env.seq(bob)).key;
        env(token::createOffer(bob, nftId, XRP(5)), token::owner(alice));

        uint256 const aliceSellOfferIndex =
            keylet::nftoffer(alice, env.seq(alice)).key;
        env(token::createOffer(alice, nftId, XRP(0)),
            token::destination(bob),
            txflags(tfSellNFToken));
        env.close();

        // bob accepts alice's offer but forgets to remove the old buy offer.
        env(token::acceptSellOffer(bob, aliceSellOfferIndex));
        env.close();

        // Note that bob still has a buy offer on the books.
        BEAST_EXPECT(env.le(keylet::nftoffer(bobBuyOfferIndex)));

        // Bob creates a sell offer for the gift NFT from alice.
        uint256 const bobSellOfferIndex =
            keylet::nftoffer(bob, env.seq(bob)).key;
        env(token::createOffer(bob, nftId, XRP(4)), txflags(tfSellNFToken));
        env.close();

        // bob now has a buy offer and a sell offer on the books.  A broker
        // spots this and swoops in to make a profit.
        BEAST_EXPECT(nftCount(env, bob) == 1);
        auto const bobsPriorBalance = env.balance(bob);
        auto const brokersPriorBalance = env.balance(broker);
        TER expectTer = features[fixNonFungibleTokensV1_2]
            ? TER(tecCANT_ACCEPT_OWN_NFTOKEN_OFFER)
            : TER(tesSUCCESS);
        env(token::brokerOffers(broker, bobBuyOfferIndex, bobSellOfferIndex),
            token::brokerFee(XRP(1)),
            ter(expectTer));
        env.close();

        if (expectTer == tesSUCCESS)
        {
            // bob should still have the NFT from alice, but be XRP(1) poorer.
            // broker should be almost XRP(1) richer because they also paid a
            // transaction fee.
            BEAST_EXPECT(nftCount(env, bob) == 1);
            BEAST_EXPECT(env.balance(bob) == bobsPriorBalance - XRP(1));
            BEAST_EXPECT(
                env.balance(broker) == brokersPriorBalance + XRP(1) - baseFee);
        }
        else
        {
            // A tec result was returned, so no state should change other
            // than the broker burning their transaction fee.
            BEAST_EXPECT(nftCount(env, bob) == 1);
            BEAST_EXPECT(env.balance(bob) == bobsPriorBalance);
            BEAST_EXPECT(env.balance(broker) == brokersPriorBalance - baseFee);
        }
    }

    void
    testFixNFTokenRemint(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("fixNFTokenRemint");

        // Returns the current ledger sequence
        auto openLedgerSeq = [](Env& env) { return env.current()->seq(); };

        // Close the ledger until the ledger sequence is large enough to delete
        // the account (no longer within <Sequence + 256>)
        // This is enforced by the featureDeletableAccounts amendment
        auto incLgrSeqForAcctDel = [&](Env& env, Account const& acct) {
            int const delta = [&]() -> int {
                if (env.seq(acct) + 255 > openLedgerSeq(env))
                    return env.seq(acct) - openLedgerSeq(env) + 255;
                return 0;
            }();
            BEAST_EXPECT(delta >= 0);
            for (int i = 0; i < delta; ++i)
                env.close();
            BEAST_EXPECT(openLedgerSeq(env) == env.seq(acct) + 255);
        };

        // Close the ledger until the ledger sequence is no longer
        // within <FirstNFTokenSequence + MintedNFTokens + 256>.
        // This is enforced by the fixNFTokenRemint amendment.
        auto incLgrSeqForFixNftRemint = [&](Env& env, Account const& acct) {
            int delta = 0;
            auto const deletableLgrSeq =
                (*env.le(acct))[~sfFirstNFTokenSequence].value_or(0) +
                (*env.le(acct))[sfMintedNFTokens] + 255;

            if (deletableLgrSeq > openLedgerSeq(env))
                delta = deletableLgrSeq - openLedgerSeq(env);

            BEAST_EXPECT(delta >= 0);
            for (int i = 0; i < delta; ++i)
                env.close();
            BEAST_EXPECT(openLedgerSeq(env) == deletableLgrSeq);
        };

        // We check if NFTokenIDs can be duplicated by
        // re-creation of an account
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const becky("becky");

            env.fund(XRP(10000), alice, becky);
            env.close();

            // alice mint and burn a NFT
            uint256 const prevNFTokenID = token::getNextID(env, alice, 0u);
            env(token::mint(alice));
            env.close();
            env(token::burn(alice, prevNFTokenID));
            env.close();

            // alice has minted 1 NFToken
            BEAST_EXPECT((*env.le(alice))[sfMintedNFTokens] == 1);

            // Close enough ledgers to delete alice's account
            incLgrSeqForAcctDel(env, alice);

            // alice's account is deleted
            Keylet const aliceAcctKey{keylet::account(alice.id())};
            auto const acctDelFee{drops(env.current()->fees().increment)};
            env(acctdelete(alice, becky), fee(acctDelFee));
            env.close();

            // alice's account root is gone from the most recently
            // closed ledger and the current ledger.
            BEAST_EXPECT(!env.closed()->exists(aliceAcctKey));
            BEAST_EXPECT(!env.current()->exists(aliceAcctKey));

            // Fund alice to re-create her account
            env.fund(XRP(10000), alice);
            env.close();

            // alice's account now exists and has minted 0 NFTokens
            BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
            BEAST_EXPECT(env.current()->exists(aliceAcctKey));
            BEAST_EXPECT((*env.le(alice))[sfMintedNFTokens] == 0);

            // alice mints a NFT with same params as prevNFTokenID
            uint256 const remintNFTokenID = token::getNextID(env, alice, 0u);
            env(token::mint(alice));
            env.close();

            // burn the NFT to make sure alice owns remintNFTokenID
            env(token::burn(alice, remintNFTokenID));
            env.close();

            if (features[fixNFTokenRemint])
                // Check that two NFTs don't have the same ID
                BEAST_EXPECT(remintNFTokenID != prevNFTokenID);
            else
                // Check that two NFTs have the same ID
                BEAST_EXPECT(remintNFTokenID == prevNFTokenID);
        }

        // Test if the issuer account can be deleted after an authorized
        // minter mints and burns a batch of NFTokens.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const becky("becky");
            Account const minter{"minter"};

            env.fund(XRP(10000), alice, becky, minter);
            env.close();

            // alice sets minter as her authorized minter
            env(token::setMinter(alice, minter));
            env.close();

            // minter mints 500 NFTs for alice
            std::vector<uint256> nftIDs;
            nftIDs.reserve(500);
            for (int i = 0; i < 500; i++)
            {
                uint256 const nftokenID = token::getNextID(env, alice, 0u);
                nftIDs.push_back(nftokenID);
                env(token::mint(minter), token::issuer(alice));
            }
            env.close();

            // minter burns 500 NFTs
            for (auto const nftokenID : nftIDs)
            {
                env(token::burn(minter, nftokenID));
            }
            env.close();

            // Increment ledger sequence to the number that is
            // enforced by the featureDeletableAccounts amendment
            incLgrSeqForAcctDel(env, alice);

            // Verify that alice's account root is present.
            Keylet const aliceAcctKey{keylet::account(alice.id())};
            BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
            BEAST_EXPECT(env.current()->exists(aliceAcctKey));

            auto const acctDelFee{drops(env.current()->fees().increment)};

            if (!features[fixNFTokenRemint])
            {
                // alice's account can be successfully deleted.
                env(acctdelete(alice, becky), fee(acctDelFee));
                env.close();
                BEAST_EXPECT(!env.current()->exists(aliceAcctKey));

                // Fund alice to re-create her account
                env.fund(XRP(10000), alice);
                env.close();

                // alice's account now exists and has minted 0 NFTokens
                BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
                BEAST_EXPECT(env.current()->exists(aliceAcctKey));
                BEAST_EXPECT((*env.le(alice))[sfMintedNFTokens] == 0);

                // alice mints a NFT with same params as the first one before
                // the account delete.
                uint256 const remintNFTokenID =
                    token::getNextID(env, alice, 0u);
                env(token::mint(alice));
                env.close();

                // burn the NFT to make sure alice owns remintNFTokenID
                env(token::burn(alice, remintNFTokenID));
                env.close();

                // The new NFT minted has the same ID as one of the NFTs
                // authorized minter minted for alice
                BEAST_EXPECT(
                    std::find(nftIDs.begin(), nftIDs.end(), remintNFTokenID) !=
                    nftIDs.end());
            }
            else if (features[fixNFTokenRemint])
            {
                // alice tries to delete her account, but is unsuccessful.
                // Due to authorized minting, alice's account sequence does not
                // advance while minter mints NFTokens for her.
                // The new account deletion retriction <FirstNFTokenSequence +
                // MintedNFTokens + 256> enabled by this amendment will enforce
                // alice to wait for more ledgers to close before she can
                // delete her account, to prevent duplicate NFTokenIDs
                env(acctdelete(alice, becky),
                    fee(acctDelFee),
                    ter(tecTOO_SOON));
                env.close();

                // alice's account is still present
                BEAST_EXPECT(env.current()->exists(aliceAcctKey));

                // Close more ledgers until it is no longer within
                // <FirstNFTokenSequence + MintedNFTokens + 256>
                // to be able to delete alice's account
                incLgrSeqForFixNftRemint(env, alice);

                // alice's account is deleted
                env(acctdelete(alice, becky), fee(acctDelFee));
                env.close();

                // alice's account root is gone from the most recently
                // closed ledger and the current ledger.
                BEAST_EXPECT(!env.closed()->exists(aliceAcctKey));
                BEAST_EXPECT(!env.current()->exists(aliceAcctKey));

                // Fund alice to re-create her account
                env.fund(XRP(10000), alice);
                env.close();

                // alice's account now exists and has minted 0 NFTokens
                BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
                BEAST_EXPECT(env.current()->exists(aliceAcctKey));
                BEAST_EXPECT((*env.le(alice))[sfMintedNFTokens] == 0);

                // alice mints a NFT with same params as the first one before
                // the account delete.
                uint256 const remintNFTokenID =
                    token::getNextID(env, alice, 0u);
                env(token::mint(alice));
                env.close();

                // burn the NFT to make sure alice owns remintNFTokenID
                env(token::burn(alice, remintNFTokenID));
                env.close();

                // The new NFT minted will not have the same ID
                // as any of the NFTs authorized minter minted
                BEAST_EXPECT(
                    std::find(nftIDs.begin(), nftIDs.end(), remintNFTokenID) ==
                    nftIDs.end());
            }
        }

        // When an account mints and burns a batch of NFTokens using tickets,
        // see if the account can be deleted.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const becky{"becky"};
            env.fund(XRP(10000), alice, becky);
            env.close();

            // alice grab enough tickets for all of the following
            // transactions.  Note that once the tickets are acquired alice's
            // account sequence number should not advance.
            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 100));
            env.close();

            BEAST_EXPECT(ticketCount(env, alice) == 100);
            BEAST_EXPECT(ownerCount(env, alice) == 100);

            // alice mints 50 NFTs using tickets
            std::vector<uint256> nftIDs;
            nftIDs.reserve(50);
            for (int i = 0; i < 50; i++)
            {
                nftIDs.push_back(token::getNextID(env, alice, 0u));
                env(token::mint(alice, 0u), ticket::use(aliceTicketSeq++));
                env.close();
            }

            // alice burns 50 NFTs using tickets
            for (auto const nftokenID : nftIDs)
            {
                env(token::burn(alice, nftokenID),
                    ticket::use(aliceTicketSeq++));
            }
            env.close();

            BEAST_EXPECT(ticketCount(env, alice) == 0);

            // Increment ledger sequence to the number that is
            // enforced by the featureDeletableAccounts amendment
            incLgrSeqForAcctDel(env, alice);

            // Verify that alice's account root is present.
            Keylet const aliceAcctKey{keylet::account(alice.id())};
            BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
            BEAST_EXPECT(env.current()->exists(aliceAcctKey));

            auto const acctDelFee{drops(env.current()->fees().increment)};

            if (!features[fixNFTokenRemint])
            {
                // alice tries to delete her account, and is successful.
                env(acctdelete(alice, becky), fee(acctDelFee));
                env.close();

                // alice's account root is gone from the most recently
                // closed ledger and the current ledger.
                BEAST_EXPECT(!env.closed()->exists(aliceAcctKey));
                BEAST_EXPECT(!env.current()->exists(aliceAcctKey));

                // Fund alice to re-create her account
                env.fund(XRP(10000), alice);
                env.close();

                // alice's account now exists and has minted 0 NFTokens
                BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
                BEAST_EXPECT(env.current()->exists(aliceAcctKey));
                BEAST_EXPECT((*env.le(alice))[sfMintedNFTokens] == 0);

                // alice mints a NFT with same params as the first one before
                // the account delete.
                uint256 const remintNFTokenID =
                    token::getNextID(env, alice, 0u);
                env(token::mint(alice));
                env.close();

                // burn the NFT to make sure alice owns remintNFTokenID
                env(token::burn(alice, remintNFTokenID));
                env.close();

                // The new NFT minted will have the same ID
                // as one of NFTs minted using tickets
                BEAST_EXPECT(
                    std::find(nftIDs.begin(), nftIDs.end(), remintNFTokenID) !=
                    nftIDs.end());
            }
            else if (features[fixNFTokenRemint])
            {
                // alice tries to delete her account, but is unsuccessful.
                // Due to authorized minting, alice's account sequence does not
                // advance while minter mints NFTokens for her using tickets.
                // The new account deletion retriction <FirstNFTokenSequence +
                // MintedNFTokens + 256> enabled by this amendment will enforce
                // alice to wait for more ledgers to close before she can
                // delete her account, to prevent duplicate NFTokenIDs
                env(acctdelete(alice, becky),
                    fee(acctDelFee),
                    ter(tecTOO_SOON));
                env.close();

                // alice's account is still present
                BEAST_EXPECT(env.current()->exists(aliceAcctKey));

                // Close more ledgers until it is no longer within
                // <FirstNFTokenSequence + MintedNFTokens + 256>
                // to be able to delete alice's account
                incLgrSeqForFixNftRemint(env, alice);

                // alice's account is deleted
                env(acctdelete(alice, becky), fee(acctDelFee));
                env.close();

                // alice's account root is gone from the most recently
                // closed ledger and the current ledger.
                BEAST_EXPECT(!env.closed()->exists(aliceAcctKey));
                BEAST_EXPECT(!env.current()->exists(aliceAcctKey));

                // Fund alice to re-create her account
                env.fund(XRP(10000), alice);
                env.close();

                // alice's account now exists and has minted 0 NFTokens
                BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
                BEAST_EXPECT(env.current()->exists(aliceAcctKey));
                BEAST_EXPECT((*env.le(alice))[sfMintedNFTokens] == 0);

                // alice mints a NFT with same params as the first one before
                // the account delete.
                uint256 const remintNFTokenID =
                    token::getNextID(env, alice, 0u);
                env(token::mint(alice));
                env.close();

                // burn the NFT to make sure alice owns remintNFTokenID
                env(token::burn(alice, remintNFTokenID));
                env.close();

                // The new NFT minted will not have the same ID
                // as any of the NFTs authorized minter minted using tickets
                BEAST_EXPECT(
                    std::find(nftIDs.begin(), nftIDs.end(), remintNFTokenID) ==
                    nftIDs.end());
            }
        }
        // If fixNFTokenRemint is enabled,
        // when an authorized minter mints and burns a batch of NFTokens using
        // tickets, issuer's account needs to wait a longer time before it can
        // deleted.
        // After the issuer's account is re-created and mints a NFT, it should
        // not have the same NFTokenID as the ones authorized minter minted.
        if (features[fixNFTokenRemint])
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const becky("becky");
            Account const minter{"minter"};

            env.fund(XRP(10000), alice, becky, minter);
            env.close();

            // alice sets minter as her authorized minter
            env(token::setMinter(alice, minter));
            env.close();

            // minter creates 100 tickets
            std::uint32_t minterTicketSeq{env.seq(minter) + 1};
            env(ticket::create(minter, 100));
            env.close();

            BEAST_EXPECT(ticketCount(env, minter) == 100);
            BEAST_EXPECT(ownerCount(env, minter) == 100);

            // minter mints 50 NFTs for alice using tickets
            std::vector<uint256> nftIDs;
            nftIDs.reserve(50);
            for (int i = 0; i < 50; i++)
            {
                uint256 const nftokenID = token::getNextID(env, alice, 0u);
                nftIDs.push_back(nftokenID);
                env(token::mint(minter),
                    token::issuer(alice),
                    ticket::use(minterTicketSeq++));
            }
            env.close();

            // minter burns 50 NFTs using tickets
            for (auto const nftokenID : nftIDs)
            {
                env(token::burn(minter, nftokenID),
                    ticket::use(minterTicketSeq++));
            }
            env.close();

            BEAST_EXPECT(ticketCount(env, minter) == 0);

            // Increment ledger sequence to the number that is
            // enforced by the featureDeletableAccounts amendment
            incLgrSeqForAcctDel(env, alice);

            // Verify that alice's account root is present.
            Keylet const aliceAcctKey{keylet::account(alice.id())};
            BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
            BEAST_EXPECT(env.current()->exists(aliceAcctKey));

            // alice tries to delete her account, but is unsuccessful.
            // Due to authorized minting, alice's account sequence does not
            // advance while minter mints NFTokens for her using tickets.
            // The new account deletion retriction <FirstNFTokenSequence +
            // MintedNFTokens + 256> enabled by this amendment will enforce
            // alice to wait for more ledgers to close before she can delete her
            // account, to prevent duplicate NFTokenIDs
            auto const acctDelFee{drops(env.current()->fees().increment)};
            env(acctdelete(alice, becky), fee(acctDelFee), ter(tecTOO_SOON));
            env.close();

            // alice's account is still present
            BEAST_EXPECT(env.current()->exists(aliceAcctKey));

            // Close more ledgers until it is no longer within
            // <FirstNFTokenSequence + MintedNFTokens + 256>
            // to be able to delete alice's account
            incLgrSeqForFixNftRemint(env, alice);

            // alice's account is deleted
            env(acctdelete(alice, becky), fee(acctDelFee));
            env.close();

            // alice's account root is gone from the most recently
            // closed ledger and the current ledger.
            BEAST_EXPECT(!env.closed()->exists(aliceAcctKey));
            BEAST_EXPECT(!env.current()->exists(aliceAcctKey));

            // Fund alice to re-create her account
            env.fund(XRP(10000), alice);
            env.close();

            // alice's account now exists and has minted 0 NFTokens
            BEAST_EXPECT(env.closed()->exists(aliceAcctKey));
            BEAST_EXPECT(env.current()->exists(aliceAcctKey));
            BEAST_EXPECT((*env.le(alice))[sfMintedNFTokens] == 0);

            // The new NFT minted will not have the same ID
            // as any of the NFTs authorized minter minted using tickets
            uint256 const remintNFTokenID = token::getNextID(env, alice, 0u);
            env(token::mint(alice));
            env.close();

            // burn the NFT to make sure alice owns remintNFTokenID
            env(token::burn(alice, remintNFTokenID));
            env.close();

            // The new NFT minted will not have the same ID
            // as one of NFTs authorized minter minted using tickets
            BEAST_EXPECT(
                std::find(nftIDs.begin(), nftIDs.end(), remintNFTokenID) ==
                nftIDs.end());
        }
    }

    void
    testFeatMintWithOffer(FeatureBitset features)
    {
        testcase("NFTokenMint with Create NFTokenOffer");

        using namespace test::jtx;

        if (!features[featureNFTokenMintOffer])
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const buyer("buyer");

            env.fund(XRP(10000), alice, buyer);
            env.close();

            env(token::mint(alice),
                token::amount(XRP(10000)),
                ter(temDISABLED));
            env.close();

            env(token::mint(alice),
                token::destination("buyer"),
                ter(temDISABLED));
            env.close();

            env(token::mint(alice),
                token::expiration(lastClose(env) + 25),
                ter(temDISABLED));
            env.close();

            return;
        }

        // The remaining tests assume featureNFTokenMintOffer is enabled.
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            Account const alice("alice");
            Account const buyer{"buyer"};
            Account const gw("gw");
            Account const issuer("issuer");
            Account const minter("minter");
            Account const bob("bob");
            IOU const gwAUD(gw["AUD"]);

            env.fund(XRP(10000), alice, buyer, gw, issuer, minter);
            env.close();

            {
                // Destination field specified but Amount field not specified
                env(token::mint(alice),
                    token::destination(buyer),
                    ter(temMALFORMED));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // Expiration field specified but Amount field not specified
                env(token::mint(alice),
                    token::expiration(lastClose(env) + 25),
                    ter(temMALFORMED));
                env.close();
                BEAST_EXPECT(ownerCount(env, buyer) == 0);
            }

            {
                // The destination may not be the account submitting the
                // transaction.
                env(token::mint(alice),
                    token::amount(XRP(1000)),
                    token::destination(alice),
                    ter(temMALFORMED));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // The destination must be an account already established in the
                // ledger.
                env(token::mint(alice),
                    token::amount(XRP(1000)),
                    token::destination(Account("demon")),
                    ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);
            }

            {
                // Set a bad expiration.
                env(token::mint(alice),
                    token::amount(XRP(1000)),
                    token::expiration(0),
                    ter(temBAD_EXPIRATION));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // The new NFTokenOffer may not have passed its expiration time.
                env(token::mint(alice),
                    token::amount(XRP(1000)),
                    token::expiration(lastClose(env)),
                    ter(tecEXPIRED));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);
            }

            {
                // Set an invalid amount.
                env(token::mint(alice),
                    token::amount(buyer["USD"](1)),
                    txflags(tfOnlyXRP),
                    ter(temBAD_AMOUNT));
                env(token::mint(alice),
                    token::amount(buyer["USD"](0)),
                    ter(temBAD_AMOUNT));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // Issuer (alice) must have a trust line for the offered funds.
                env(token::mint(alice),
                    token::amount(gwAUD(1000)),
                    txflags(tfTransferable),
                    token::xferFee(10),
                    ter(tecNO_LINE));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // If the IOU issuer and the NFToken issuer are the same,
                // then that issuer does not need a trust line to accept their
                // fee.
                env(token::mint(gw),
                    token::amount(gwAUD(1000)),
                    txflags(tfTransferable),
                    token::xferFee(10));
                env.close();

                // Give alice the needed trust line, but freeze it.
                env(trust(gw, alice["AUD"](999), tfSetFreeze));
                env.close();

                // Issuer (alice) must have a trust line for the offered funds
                // and the trust line may not be frozen.
                env(token::mint(alice),
                    token::amount(gwAUD(1000)),
                    txflags(tfTransferable),
                    token::xferFee(10),
                    ter(tecFROZEN));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // Seller (alice) must have a trust line may not be frozen.
                env(token::mint(alice),
                    token::amount(gwAUD(1000)),
                    ter(tecFROZEN));
                env.close();
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // Unfreeze alice's trustline.
                env(trust(gw, alice["AUD"](999), tfClearFreeze));
                env.close();
            }

            {
                // check reserve
                auto const acctReserve =
                    env.current()->fees().accountReserve(0);
                auto const incReserve = env.current()->fees().increment;

                env.fund(acctReserve + incReserve, bob);
                env.close();

                // doesn't have reserve for 2 objects (NFTokenPage, Offer)
                env(token::mint(bob),
                    token::amount(XRP(0)),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                // have reserve for NFTokenPage, Offer
                env(pay(env.master, bob, incReserve + drops(baseFee)));
                env.close();
                env(token::mint(bob), token::amount(XRP(0)));
                env.close();

                // doesn't have reserve for Offer
                env(pay(env.master, bob, drops(baseFee)));
                env.close();
                env(token::mint(bob),
                    token::amount(XRP(0)),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                // have reserve for Offer
                env(pay(env.master, bob, incReserve + drops(baseFee)));
                env.close();
                env(token::mint(bob), token::amount(XRP(0)));
                env.close();
            }

            // Amount field specified
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            env(token::mint(alice), token::amount(XRP(10)));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            env.close();

            // Amount field and Destination field, Expiration field specified
            env(token::mint(alice),
                token::amount(XRP(10)),
                token::destination(buyer),
                token::expiration(lastClose(env) + 25));
            env.close();

            // With TransferFee field
            env(trust(alice, gwAUD(1000)));
            env.close();
            env(token::mint(alice),
                token::amount(gwAUD(1)),
                token::destination(buyer),
                token::expiration(lastClose(env) + 25),
                txflags(tfTransferable),
                token::xferFee(10));
            env.close();

            // Can be canceled by the issuer.
            env(token::mint(alice),
                token::amount(XRP(10)),
                token::destination(buyer),
                token::expiration(lastClose(env) + 25));
            uint256 const offerAliceSellsToBuyer =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::cancelOffer(alice, {offerAliceSellsToBuyer}));
            env.close();

            // Can be canceled by the buyer.
            env(token::mint(buyer),
                token::amount(XRP(10)),
                token::destination(alice),
                token::expiration(lastClose(env) + 25));
            uint256 const offerBuyerSellsToAlice =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::cancelOffer(alice, {offerBuyerSellsToAlice}));
            env.close();

            env(token::setMinter(issuer, minter));
            env.close();

            // Minter will have offer not issuer
            BEAST_EXPECT(ownerCount(env, minter) == 0);
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            env(token::mint(minter),
                token::issuer(issuer),
                token::amount(drops(1)));
            env.close();
            BEAST_EXPECT(ownerCount(env, minter) == 2);
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
        }

        // Test sell offers with a destination with and without
        // fixNFTokenNegOffer.
        for (auto const& tweakedFeatures :
             {features - fixNFTokenNegOffer - featureNonFungibleTokensV1_1,
              features | fixNFTokenNegOffer})
        {
            Env env{*this, tweakedFeatures};
            Account const alice("alice");

            env.fund(XRP(1000000), alice);

            TER const offerCreateTER = tweakedFeatures[fixNFTokenNegOffer]
                ? static_cast<TER>(temBAD_AMOUNT)
                : static_cast<TER>(tesSUCCESS);

            // Make offers with negative amounts for the NFTs
            env(token::mint(alice),
                token::amount(XRP(-2)),
                ter(offerCreateTER));
            env.close();
        }
    }

    void
    testTxJsonMetaFields(FeatureBitset features)
    {
        // `nftoken_id` is added in the `tx` response for NFTokenMint and
        // NFTokenAcceptOffer.
        //
        // `nftoken_ids` is added in the `tx` response for NFTokenCancelOffer
        //
        // `offer_id` is added in the `tx` response for NFTokenCreateOffer
        //
        // The values of these fields are dependent on the NFTokenID/OfferID
        // changed in its corresponding transaction. We want to validate each
        // transaction to make sure the synethic fields hold the right values.

        testcase("Test synthetic fields from JSON response");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const broker{"broker"};

        Env env{*this, features};
        env.fund(XRP(10000), alice, bob, broker);
        env.close();

        // Verify `nftoken_id` value equals to the NFTokenID that was
        // changed in the most recent NFTokenMint or NFTokenAcceptOffer
        // transaction
        auto verifyNFTokenID = [&](uint256 const& actualNftID) {
            // Get the hash for the most recent transaction.
            std::string const txHash{
                env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

            env.close();
            Json::Value const meta =
                env.rpc("tx", txHash)[jss::result][jss::meta];

            // Expect nftokens_id field
            if (!BEAST_EXPECT(meta.isMember(jss::nftoken_id)))
                return;

            // Check the value of NFT ID in the meta with the
            // actual value
            uint256 nftID;
            BEAST_EXPECT(nftID.parseHex(meta[jss::nftoken_id].asString()));
            BEAST_EXPECT(nftID == actualNftID);
        };

        // Verify `nftoken_ids` value equals to the NFTokenIDs that were
        // changed in the most recent NFTokenCancelOffer transaction
        auto verifyNFTokenIDsInCancelOffer =
            [&](std::vector<uint256> actualNftIDs) {
                // Get the hash for the most recent transaction.
                std::string const txHash{
                    env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

                env.close();
                Json::Value const meta =
                    env.rpc("tx", txHash)[jss::result][jss::meta];

                // Expect nftokens_ids field and verify the values
                if (!BEAST_EXPECT(meta.isMember(jss::nftoken_ids)))
                    return;

                // Convert NFT IDs from Json::Value to uint256
                std::vector<uint256> metaIDs;
                std::transform(
                    meta[jss::nftoken_ids].begin(),
                    meta[jss::nftoken_ids].end(),
                    std::back_inserter(metaIDs),
                    [this](Json::Value id) {
                        uint256 nftID;
                        BEAST_EXPECT(nftID.parseHex(id.asString()));
                        return nftID;
                    });

                // Sort both array to prepare for comparison
                std::sort(metaIDs.begin(), metaIDs.end());
                std::sort(actualNftIDs.begin(), actualNftIDs.end());

                // Make sure the expect number of NFTs is correct
                BEAST_EXPECT(metaIDs.size() == actualNftIDs.size());

                // Check the value of NFT ID in the meta with the
                // actual values
                for (size_t i = 0; i < metaIDs.size(); ++i)
                    BEAST_EXPECT(metaIDs[i] == actualNftIDs[i]);
            };

        // Verify `offer_id` value equals to the offerID that was
        // changed in the most recent NFTokenCreateOffer tx
        auto verifyNFTokenOfferID = [&](uint256 const& offerID) {
            // Get the hash for the most recent transaction.
            std::string const txHash{
                env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

            env.close();
            Json::Value const meta =
                env.rpc("tx", txHash)[jss::result][jss::meta];

            // Expect offer_id field and verify the value
            if (!BEAST_EXPECT(meta.isMember(jss::offer_id)))
                return;

            uint256 metaOfferID;
            BEAST_EXPECT(metaOfferID.parseHex(meta[jss::offer_id].asString()));
            BEAST_EXPECT(metaOfferID == offerID);
        };

        // Check new fields in tx meta when for all NFTtransactions
        {
            // Alice mints 2 NFTs
            // Verify the NFTokenIDs are correct in the NFTokenMint tx meta
            uint256 const nftId1{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            verifyNFTokenID(nftId1);

            uint256 const nftId2{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            verifyNFTokenID(nftId2);

            // Alice creates one sell offer for each NFT
            // Verify the offer indexes are correct in the NFTokenCreateOffer tx
            // meta
            uint256 const aliceOfferIndex1 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId1, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(aliceOfferIndex1);

            uint256 const aliceOfferIndex2 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId2, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(aliceOfferIndex2);

            // Alice cancels two offers she created
            // Verify the NFTokenIDs are correct in the NFTokenCancelOffer tx
            // meta
            env(token::cancelOffer(
                alice, {aliceOfferIndex1, aliceOfferIndex2}));
            env.close();
            verifyNFTokenIDsInCancelOffer({nftId1, nftId2});

            // Bobs creates a buy offer for nftId1
            // Verify the offer id is correct in the NFTokenCreateOffer tx meta
            auto const bobBuyOfferIndex =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId1, drops(1)), token::owner(alice));
            env.close();
            verifyNFTokenOfferID(bobBuyOfferIndex);

            // Alice accepts bob's buy offer
            // Verify the NFTokenID is correct in the NFTokenAcceptOffer tx meta
            env(token::acceptBuyOffer(alice, bobBuyOfferIndex));
            env.close();
            verifyNFTokenID(nftId1);
        }

        // Check `nftoken_ids` in brokered mode
        {
            // Alice mints a NFT
            uint256 const nftId{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            verifyNFTokenID(nftId);

            // Alice creates sell offer and set broker as destination
            uint256 const offerAliceToBroker =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)),
                token::destination(broker),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(offerAliceToBroker);

            // Bob creates buy offer
            uint256 const offerBobToBroker =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId, drops(1)), token::owner(alice));
            env.close();
            verifyNFTokenOfferID(offerBobToBroker);

            // Check NFTokenID meta for NFTokenAcceptOffer in brokered mode
            env(token::brokerOffers(
                broker, offerBobToBroker, offerAliceToBroker));
            env.close();
            verifyNFTokenID(nftId);
        }

        // Check if there are no duplicate nft id in Cancel transactions where
        // multiple offers are cancelled for the same NFT
        {
            // Alice mints a NFT
            uint256 const nftId{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            verifyNFTokenID(nftId);

            // Alice creates 2 sell offers for the same NFT
            uint256 const aliceOfferIndex1 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(aliceOfferIndex1);

            uint256 const aliceOfferIndex2 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(aliceOfferIndex2);

            // Make sure the metadata only has 1 nft id, since both offers are
            // for the same nft
            env(token::cancelOffer(
                alice, {aliceOfferIndex1, aliceOfferIndex2}));
            env.close();
            verifyNFTokenIDsInCancelOffer({nftId});
        }

        if (features[featureNFTokenMintOffer])
        {
            uint256 const aliceMintWithOfferIndex1 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::mint(alice), token::amount(XRP(0)));
            env.close();
            verifyNFTokenOfferID(aliceMintWithOfferIndex1);
        }
    }

    void
    testFixNFTokenBuyerReserve(FeatureBitset features)
    {
        testcase("Test buyer reserve when accepting an offer");

        using namespace test::jtx;

        // Lambda that mints an NFT and then creates a sell offer
        auto mintAndCreateSellOffer = [](test::jtx::Env& env,
                                         test::jtx::Account const& acct,
                                         STAmount const amt) -> uint256 {
            // acct mints a NFT
            uint256 const nftId{
                token::getNextID(env, acct, 0u, tfTransferable)};
            env(token::mint(acct, 0u), txflags(tfTransferable));
            env.close();

            // acct makes an sell offer
            uint256 const sellOfferIndex =
                keylet::nftoffer(acct, env.seq(acct)).key;
            env(token::createOffer(acct, nftId, amt), txflags(tfSellNFToken));
            env.close();

            return sellOfferIndex;
        };

        // Test the behaviors when the buyer makes an accept offer, both before
        // and after enabling the amendment. Exercises the precise number of
        // reserve in drops that's required to accept the offer
        {
            Account const alice{"alice"};
            Account const bob{"bob"};

            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(10000), alice);
            env.close();

            // Bob is funded with minimum XRP reserve
            env.fund(acctReserve, bob);
            env.close();

            // alice mints an NFT and create a sell offer for 0 XRP
            auto const sellOfferIndex =
                mintAndCreateSellOffer(env, alice, XRP(0));

            // Bob owns no object
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // Without fixNFTokenReserve amendment, when bob accepts an NFT sell
            // offer, he can get the NFT free of reserve
            if (!features[fixNFTokenReserve])
            {
                // Bob is able to accept the offer
                env(token::acceptSellOffer(bob, sellOfferIndex));
                env.close();

                // Bob now owns an extra objects
                BEAST_EXPECT(ownerCount(env, bob) == 1);

                // This is the wrong behavior, since Bob should need at least
                // one incremental reserve.
            }
            // With fixNFTokenReserve, bob can no longer accept the offer unless
            // there is enough reserve. A detail to note is that NFTs(sell
            // offer) will not allow one to go below the reserve requirement,
            // because buyer's balance is computed after the transaction fee is
            // deducted. This means that the reserve requirement will be `base
            // fee` drops higher than normal.
            else
            {
                // Bob is not able to accept the offer with only the account
                // reserve (200,000,000 drops)
                env(token::acceptSellOffer(bob, sellOfferIndex),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                // after prev transaction, Bob owns `200M - base fee` drops due
                // to burnt tx fee

                BEAST_EXPECT(ownerCount(env, bob) == 0);

                // Send bob an increment reserve and base fee (to make up for
                // the transaction fee burnt from the prev failed tx) Bob now
                // owns 250,000,000 drops
                env(pay(env.master, bob, incReserve + drops(baseFee)));
                env.close();

                // However, this transaction will still fail because the reserve
                // requirement is `base fee` drops higher
                env(token::acceptSellOffer(bob, sellOfferIndex),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                // Send bob `base fee * 2` drops
                // Bob now owns `250M + base fee` drops
                env(pay(env.master, bob, drops(baseFee * 2)));
                env.close();

                // Bob is now able to accept the offer
                env(token::acceptSellOffer(bob, sellOfferIndex));
                env.close();

                BEAST_EXPECT(ownerCount(env, bob) == 1);
            }
        }

        // Now exercise the scenario when the buyer accepts
        // many sell offers
        {
            Account const alice{"alice"};
            Account const bob{"bob"};

            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;

            env.fund(XRP(10000), alice);
            env.close();

            env.fund(acctReserve + XRP(1), bob);
            env.close();

            if (!features[fixNFTokenReserve])
            {
                // Bob can accept many NFTs without having a single reserve!
                for (size_t i = 0; i < 200; i++)
                {
                    // alice mints an NFT and creates a sell offer for 0 XRP
                    auto const sellOfferIndex =
                        mintAndCreateSellOffer(env, alice, XRP(0));

                    // Bob is able to accept the offer
                    env(token::acceptSellOffer(bob, sellOfferIndex));
                    env.close();
                }
            }
            else
            {
                // alice mints the first NFT and creates a sell offer for 0 XRP
                auto const sellOfferIndex1 =
                    mintAndCreateSellOffer(env, alice, XRP(0));

                // Bob cannot accept this offer because he doesn't have the
                // reserve for the NFT
                env(token::acceptSellOffer(bob, sellOfferIndex1),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                // Give bob enough reserve
                env(pay(env.master, bob, drops(incReserve)));
                env.close();

                BEAST_EXPECT(ownerCount(env, bob) == 0);

                // Bob now owns his first NFT
                env(token::acceptSellOffer(bob, sellOfferIndex1));
                env.close();

                BEAST_EXPECT(ownerCount(env, bob) == 1);

                // alice now mints 31 more NFTs and creates an offer for each
                // NFT, then sells to bob
                for (size_t i = 0; i < 31; i++)
                {
                    // alice mints an NFT and creates a sell offer for 0 XRP
                    auto const sellOfferIndex =
                        mintAndCreateSellOffer(env, alice, XRP(0));

                    // Bob can accept the offer because the new NFT is stored in
                    // an existing NFTokenPage so no new reserve is requried
                    env(token::acceptSellOffer(bob, sellOfferIndex));
                    env.close();
                }

                BEAST_EXPECT(ownerCount(env, bob) == 1);

                // alice now mints the 33rd NFT and creates an sell offer for 0
                // XRP
                auto const sellOfferIndex33 =
                    mintAndCreateSellOffer(env, alice, XRP(0));

                // Bob fails to accept this NFT because he does not have enough
                // reserve for a new NFTokenPage
                env(token::acceptSellOffer(bob, sellOfferIndex33),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                // Send bob incremental reserve
                env(pay(env.master, bob, drops(incReserve)));
                env.close();

                // Bob now has enough reserve to accept the offer and now
                // owns one more NFTokenPage
                env(token::acceptSellOffer(bob, sellOfferIndex33));
                env.close();

                BEAST_EXPECT(ownerCount(env, bob) == 2);
            }
        }

        // Test the behavior when the seller accepts a buy offer.
        // The behavior should not change regardless whether fixNFTokenReserve
        // is enabled or not, since the ledger is able to guard against
        // free NFTokenPages when buy offer is accepted. This is merely an
        // additional test to exercise existing offer behavior.
        {
            Account const alice{"alice"};
            Account const bob{"bob"};

            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(10000), alice);
            env.close();

            // Bob is funded with account reserve + increment reserve + 1 XRP
            // increment reserve is for the buy offer, and 1 XRP is for offer
            // price
            env.fund(acctReserve + incReserve + XRP(1), bob);
            env.close();

            // Alice mints a NFT
            uint256 const nftId{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();

            // Bob makes a buy offer for 1 XRP
            auto const buyOfferIndex = keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId, XRP(1)), token::owner(alice));
            env.close();

            // accepting the buy offer fails because bob's balance is `base fee`
            // drops lower than the required amount, since the previous tx burnt
            // drops for tx fee.
            env(token::acceptBuyOffer(alice, buyOfferIndex),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // send Bob `base fee` drops
            env(pay(env.master, bob, drops(baseFee)));
            env.close();

            // Now bob can buy the offer
            env(token::acceptBuyOffer(alice, buyOfferIndex));
            env.close();
        }

        // Test the reserve behavior in brokered mode.
        // The behavior should not change regardless whether fixNFTokenReserve
        // is enabled or not, since the ledger is able to guard against
        // free NFTokenPages in brokered mode. This is merely an
        // additional test to exercise existing offer behavior.
        {
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const broker{"broker"};

            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(10000), alice, broker);
            env.close();

            // Bob is funded with account reserve + incr reserve + 1 XRP(offer
            // price)
            env.fund(acctReserve + incReserve + XRP(1), bob);
            env.close();

            // Alice mints a NFT
            uint256 const nftId{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();

            // Alice creates sell offer and set broker as destination
            uint256 const offerAliceToBroker =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, XRP(1)),
                token::destination(broker),
                txflags(tfSellNFToken));
            env.close();

            // Bob creates buy offer
            uint256 const offerBobToBroker =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId, XRP(1)), token::owner(alice));
            env.close();

            // broker offers.
            // Returns insufficient funds, because bob burnt tx fee when he
            // created his buy offer, which makes his spendable balance to be
            // less than the required amount.
            env(token::brokerOffers(
                    broker, offerBobToBroker, offerAliceToBroker),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // send Bob `base fee` drops
            env(pay(env.master, bob, drops(baseFee)));
            env.close();

            // broker offers.
            env(token::brokerOffers(
                broker, offerBobToBroker, offerAliceToBroker));
            env.close();
        }
    }

    void
    testUnaskedForAutoTrustline(FeatureBitset features)
    {
        testcase("Test fix unasked for auto-trustline.");

        using namespace test::jtx;

        Account const issuer{"issuer"};
        Account const becky{"becky"};
        Account const cheri{"cheri"};
        Account const gw("gw");
        IOU const gwAUD(gw["AUD"]);

        // This test case covers issue...
        // https://github.com/XRPLF/rippled/issues/4925
        //
        // For an NFToken with a transfer fee, the issuer must be able to
        // accept the transfer fee or else a transfer should fail.  If the
        // NFToken is transferred for a non-XRP asset, then the issuer must
        // have a trustline to that asset to receive the fee.
        //
        // This test looks at a situation where issuer would get a trustline
        // for the fee without the issuer's consent.  Here are the steps:
        //  1. Issuer has a trustline (i.e., USD)
        //  2. Issuer mints NFToken with transfer fee.
        //  3. Becky acquires the NFToken, paying with XRP.
        //  4. Becky creates offer to sell NFToken for USD(100).
        //  5. Issuer deletes trustline for USD.
        //  6. Carol buys NFToken from Becky for USD(100).
        //  7. The transfer fee from Carol's purchase re-establishes issuer's
        //     USD trustline.
        //
        // The fixEnforceNFTokenTrustline amendment addresses this oversight.
        //
        // We run this test case both with and without
        // fixEnforceNFTokenTrustline enabled so we can see the change
        // in behavior.
        //
        // In both cases we remove the fixRemoveNFTokenAutoTrustLine amendment.
        // Otherwise we can't create NFTokens with tfTrustLine enabled.
        FeatureBitset const localFeatures =
            features - fixRemoveNFTokenAutoTrustLine;
        for (FeatureBitset feats :
             {localFeatures - fixEnforceNFTokenTrustline,
              localFeatures | fixEnforceNFTokenTrustline})
        {
            Env env{*this, feats};
            env.fund(XRP(1000), issuer, becky, cheri, gw);
            env.close();

            // Set trust lines so becky and cheri can use gw's currency.
            env(trust(becky, gwAUD(1000)));
            env(trust(cheri, gwAUD(1000)));
            env.close();
            env(pay(gw, cheri, gwAUD(500)));
            env.close();

            // issuer creates two NFTs: one with and one without AutoTrustLine.
            std::uint16_t xferFee = 5000;  // 5%
            uint256 const nftAutoTrustID{token::getNextID(
                env, issuer, 0u, tfTransferable | tfTrustLine, xferFee)};
            env(token::mint(issuer, 0u),
                token::xferFee(xferFee),
                txflags(tfTransferable | tfTrustLine));
            env.close();

            uint256 const nftNoAutoTrustID{
                token::getNextID(env, issuer, 0u, tfTransferable, xferFee)};
            env(token::mint(issuer, 0u),
                token::xferFee(xferFee),
                txflags(tfTransferable));
            env.close();

            // becky buys the nfts for 1 drop each.
            {
                uint256 const beckyBuyOfferIndex1 =
                    keylet::nftoffer(becky, env.seq(becky)).key;
                env(token::createOffer(becky, nftAutoTrustID, drops(1)),
                    token::owner(issuer));

                uint256 const beckyBuyOfferIndex2 =
                    keylet::nftoffer(becky, env.seq(becky)).key;
                env(token::createOffer(becky, nftNoAutoTrustID, drops(1)),
                    token::owner(issuer));

                env.close();
                env(token::acceptBuyOffer(issuer, beckyBuyOfferIndex1));
                env(token::acceptBuyOffer(issuer, beckyBuyOfferIndex2));
                env.close();
            }

            // becky creates offers to sell the nfts for AUD.
            uint256 const beckyAutoTrustOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftAutoTrustID, gwAUD(100)),
                txflags(tfSellNFToken));
            env.close();

            // Creating an offer for the NFToken without tfTrustLine fails
            // because issuer does not have a trust line for AUD.
            env(token::createOffer(becky, nftNoAutoTrustID, gwAUD(100)),
                txflags(tfSellNFToken),
                ter(tecNO_LINE));
            env.close();

            // issuer creates a trust line.  Now the offer create for the
            // NFToken without tfTrustLine succeeds.
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            env(trust(issuer, gwAUD(1000)));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 1);

            uint256 const beckyNoAutoTrustOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftNoAutoTrustID, gwAUD(100)),
                txflags(tfSellNFToken));
            env.close();

            // Now that the offers are in place, issuer removes the trustline.
            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            env(trust(issuer, gwAUD(0)));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);

            // cheri attempts to accept becky's offers.  Behavior with the
            // AutoTrustline NFT is uniform: issuer gets a new trust line.
            env(token::acceptSellOffer(cheri, beckyAutoTrustOfferIndex));
            env.close();

            // Here's evidence that issuer got the new AUD trust line.
            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(env.balance(issuer, gwAUD) == gwAUD(5));

            // issuer once again removes the trust line for AUD.
            env(pay(issuer, gw, gwAUD(5)));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);

            // cheri attempts to accept the NoAutoTrustLine NFT.  Behavior
            // depends on whether fixEnforceNFTokenTrustline is enabled.
            if (feats[fixEnforceNFTokenTrustline])
            {
                // With fixEnforceNFTokenTrustline cheri can't accept the
                // offer because issuer could not get their transfer fee
                // without the appropriate trustline.
                env(token::acceptSellOffer(cheri, beckyNoAutoTrustOfferIndex),
                    ter(tecNO_LINE));
                env.close();

                // But if issuer re-establishes the trustline then the offer
                // can be accepted.
                env(trust(issuer, gwAUD(1000)));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 1);

                env(token::acceptSellOffer(cheri, beckyNoAutoTrustOfferIndex));
                env.close();
            }
            else
            {
                // Without fixEnforceNFTokenTrustline the offer just works
                // and issuer gets a trustline that they did not request.
                env(token::acceptSellOffer(cheri, beckyNoAutoTrustOfferIndex));
                env.close();
            }
            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(env.balance(issuer, gwAUD) == gwAUD(5));
        }  // for feats
    }

    void
    testNFTIssuerIsIOUIssuer(FeatureBitset features)
    {
        testcase("Test fix NFT issuer is IOU issuer");

        using namespace test::jtx;

        Account const issuer{"issuer"};
        Account const becky{"becky"};
        Account const cheri{"cheri"};
        IOU const isISU(issuer["ISU"]);

        // This test case covers issue...
        // https://github.com/XRPLF/rippled/issues/4941
        //
        // If an NFToken has a transfer fee then, when an offer is accepted,
        // a portion of the sale price goes to the issuer.
        //
        // It is possible for an issuer to issue both an IOU (for remittances)
        // and NFTokens.  If the issuer's IOU is used to pay for the transfer
        // of one of the issuer's NFTokens, then paying the fee for that
        // transfer will fail with a tecNO_LINE.
        //
        // The problem occurs because the NFT code looks for a trust line to
        // pay the transfer fee.  However the issuer of an IOU does not need
        // a trust line to accept their own issuance and, in fact, is not
        // allowed to have a trust line to themselves.
        //
        // This test looks at a situation where transfer of an NFToken is
        // prevented by this bug:
        //  1. Issuer issues an IOU (e.g, isISU).
        //  2. Becky and Cheri get trust lines for, and acquire, some isISU.
        //  3. Issuer mints NFToken with transfer fee.
        //  4. Becky acquires the NFToken, paying with XRP.
        //  5. Becky attempts to create an offer to sell the NFToken for
        //     isISU(100).  The attempt fails with `tecNO_LINE`.
        //
        // The featureNFTokenMintOffer amendment addresses this oversight.
        //
        // We remove the fixRemoveNFTokenAutoTrustLine amendment.  Otherwise
        // we can't create NFTokens with tfTrustLine enabled.
        FeatureBitset const localFeatures =
            features - fixRemoveNFTokenAutoTrustLine;

        Env env{*this, localFeatures};
        env.fund(XRP(1000), issuer, becky, cheri);
        env.close();

        // Set trust lines so becky and cheri can use isISU.
        env(trust(becky, isISU(1000)));
        env(trust(cheri, isISU(1000)));
        env.close();
        env(pay(issuer, cheri, isISU(500)));
        env.close();

        // issuer creates two NFTs: one with and one without AutoTrustLine.
        std::uint16_t xferFee = 5000;  // 5%
        uint256 const nftAutoTrustID{token::getNextID(
            env, issuer, 0u, tfTransferable | tfTrustLine, xferFee)};
        env(token::mint(issuer, 0u),
            token::xferFee(xferFee),
            txflags(tfTransferable | tfTrustLine));
        env.close();

        uint256 const nftNoAutoTrustID{
            token::getNextID(env, issuer, 0u, tfTransferable, xferFee)};
        env(token::mint(issuer, 0u),
            token::xferFee(xferFee),
            txflags(tfTransferable));
        env.close();

        // becky buys the nfts for 1 drop each.
        {
            uint256 const beckyBuyOfferIndex1 =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftAutoTrustID, drops(1)),
                token::owner(issuer));

            uint256 const beckyBuyOfferIndex2 =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftNoAutoTrustID, drops(1)),
                token::owner(issuer));

            env.close();
            env(token::acceptBuyOffer(issuer, beckyBuyOfferIndex1));
            env(token::acceptBuyOffer(issuer, beckyBuyOfferIndex2));
            env.close();
        }

        // Behavior from here down diverges significantly based on
        // featureNFTokenMintOffer.
        if (!localFeatures[featureNFTokenMintOffer])
        {
            // Without featureNFTokenMintOffer becky simply can't
            // create an offer for a non-tfTrustLine NFToken that would
            // pay the transfer fee in issuer's own IOU.
            env(token::createOffer(becky, nftNoAutoTrustID, isISU(100)),
                txflags(tfSellNFToken),
                ter(tecNO_LINE));
            env.close();

            // And issuer can't create a trust line to themselves.
            env(trust(issuer, isISU(1000)), ter(temDST_IS_SRC));
            env.close();

            // However if the NFToken has the tfTrustLine flag set,
            // then becky can create the offer.
            uint256 const beckyAutoTrustOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftAutoTrustID, isISU(100)),
                txflags(tfSellNFToken));
            env.close();

            // And cheri can accept the offer.
            env(token::acceptSellOffer(cheri, beckyAutoTrustOfferIndex));
            env.close();

            // We verify that issuer got their transfer fee by seeing that
            // ISU(5) has disappeared out of cheri's and becky's balances.
            BEAST_EXPECT(env.balance(becky, isISU) == isISU(95));
            BEAST_EXPECT(env.balance(cheri, isISU) == isISU(400));
        }
        else
        {
            // With featureNFTokenMintOffer things go better.
            // becky creates offers to sell the nfts for ISU.
            uint256 const beckyNoAutoTrustOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftNoAutoTrustID, isISU(100)),
                txflags(tfSellNFToken));
            env.close();
            uint256 const beckyAutoTrustOfferIndex =
                keylet::nftoffer(becky, env.seq(becky)).key;
            env(token::createOffer(becky, nftAutoTrustID, isISU(100)),
                txflags(tfSellNFToken));
            env.close();

            // cheri accepts becky's offers.  Behavior is uniform:
            // issuer gets paid.
            env(token::acceptSellOffer(cheri, beckyAutoTrustOfferIndex));
            env.close();

            // We verify that issuer got their transfer fee by seeing that
            // ISU(5) has disappeared out of cheri's and becky's balances.
            BEAST_EXPECT(env.balance(becky, isISU) == isISU(95));
            BEAST_EXPECT(env.balance(cheri, isISU) == isISU(400));

            env(token::acceptSellOffer(cheri, beckyNoAutoTrustOfferIndex));
            env.close();

            // We verify that issuer got their transfer fee by seeing that
            // an additional ISU(5) has disappeared out of cheri's and
            // becky's balances.
            BEAST_EXPECT(env.balance(becky, isISU) == isISU(190));
            BEAST_EXPECT(env.balance(cheri, isISU) == isISU(300));
        }
    }

    void
    testNFTokenModify(FeatureBitset features)
    {
        testcase("Test NFTokenModify");

        using namespace test::jtx;

        Account const issuer{"issuer"};
        Account const alice("alice");
        Account const bob("bob");

        bool const modifyEnabled = features[featureDynamicNFT];

        {
            // Mint with tfMutable
            Env env{*this, features};
            env.fund(XRP(10000), issuer);
            env.close();

            auto const expectedTer =
                modifyEnabled ? TER{tesSUCCESS} : TER{temINVALID_FLAG};
            env(token::mint(issuer, 0u), txflags(tfMutable), ter(expectedTer));
            env.close();
        }
        {
            Env env{*this, features};
            env.fund(XRP(10000), issuer);
            env.close();

            // Modify a nftoken
            uint256 const nftId{token::getNextID(env, issuer, 0u, tfMutable)};
            if (modifyEnabled)
            {
                env(token::mint(issuer, 0u), txflags(tfMutable));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                env(token::modify(issuer, nftId));
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
            }
            else
            {
                env(token::mint(issuer, 0u));
                env.close();
                env(token::modify(issuer, nftId), ter(temDISABLED));
                env.close();
            }
        }
        if (!modifyEnabled)
            return;

        {
            Env env{*this, features};
            env.fund(XRP(10000), issuer);
            env.close();

            uint256 const nftId{token::getNextID(env, issuer, 0u, tfMutable)};
            env(token::mint(issuer, 0u), txflags(tfMutable));
            env.close();

            // Set a negative fee. Exercises invalid preflight1.
            env(token::modify(issuer, nftId),
                fee(STAmount(10ull, true)),
                ter(temBAD_FEE));
            env.close();

            // Invalid Flags
            env(token::modify(issuer, nftId),
                txflags(0x00000001),
                ter(temINVALID_FLAG));

            // Invalid Owner
            env(token::modify(issuer, nftId),
                token::owner(issuer),
                ter(temMALFORMED));
            env.close();

            // Invalid URI length = 0
            env(token::modify(issuer, nftId),
                token::uri(""),
                ter(temMALFORMED));
            env.close();

            // Invalid URI length > 256
            env(token::modify(issuer, nftId),
                token::uri(std::string(maxTokenURILength + 1, 'q')),
                ter(temMALFORMED));
            env.close();
        }
        {
            Env env{*this, features};
            env.fund(XRP(10000), issuer, alice, bob);
            env.close();

            {
                // NFToken not exists
                uint256 const nftIDNotExists{
                    token::getNextID(env, issuer, 0u, tfMutable)};
                env.close();

                env(token::modify(issuer, nftIDNotExists), ter(tecNO_ENTRY));
                env.close();
            }
            {
                // Invalid NFToken flag
                uint256 const nftIDNotModifiable{
                    token::getNextID(env, issuer, 0u)};
                env(token::mint(issuer, 0u));
                env.close();

                env(token::modify(issuer, nftIDNotModifiable),
                    ter(tecNO_PERMISSION));
                env.close();
            }
            {
                // Unauthorized account
                uint256 const nftId{
                    token::getNextID(env, issuer, 0u, tfMutable)};
                env(token::mint(issuer, 0u), txflags(tfMutable));
                env.close();

                env(token::modify(bob, nftId),
                    token::owner(issuer),
                    ter(tecNO_PERMISSION));
                env.close();

                env(token::setMinter(issuer, alice));
                env.close();

                env(token::modify(bob, nftId),
                    token::owner(issuer),
                    ter(tecNO_PERMISSION));
                env.close();
            }
        }
        {
            Env env{*this, features};
            env.fund(XRP(10000), issuer, alice, bob);
            env.close();

            // modify with tfFullyCanonicalSig should success
            uint256 const nftId{token::getNextID(env, issuer, 0u, tfMutable)};
            env(token::mint(issuer, 0u), txflags(tfMutable), token::uri("uri"));
            env.close();

            env(token::modify(issuer, nftId), txflags(tfFullyCanonicalSig));
            env.close();
        }
        {
            Env env{*this, features};
            env.fund(XRP(10000), issuer, alice, bob);
            env.close();

            // lambda that returns the JSON form of NFTokens held by acct
            auto accountNFTs = [&env](Account const& acct) {
                Json::Value params;
                params[jss::account] = acct.human();
                params[jss::type] = "state";
                auto response =
                    env.rpc("json", "account_nfts", to_string(params));
                return response[jss::result][jss::account_nfts];
            };

            // lambda that checks for the expected URI value of an NFToken
            auto checkURI = [&accountNFTs, this](
                                Account const& acct,
                                char const* uri,
                                int line) {
                auto const nfts = accountNFTs(acct);
                if (nfts.size() == 1)
                    pass();
                else
                {
                    std::ostringstream text;
                    text << "checkURI: unexpected NFT count on line " << line;
                    fail(text.str(), __FILE__, line);
                    return;
                }

                if (uri == nullptr)
                {
                    if (!nfts[0u].isMember(sfURI.jsonName))
                        pass();
                    else
                    {
                        std::ostringstream text;
                        text << "checkURI: unexpected URI present on line "
                             << line;
                        fail(text.str(), __FILE__, line);
                    }
                    return;
                }

                if (nfts[0u][sfURI.jsonName] == strHex(std::string(uri)))
                    pass();
                else
                {
                    std::ostringstream text;
                    text << "checkURI: unexpected URI contents on line "
                         << line;
                    fail(text.str(), __FILE__, line);
                }
            };

            uint256 const nftId{token::getNextID(env, issuer, 0u, tfMutable)};
            env.close();

            env(token::mint(issuer, 0u), txflags(tfMutable), token::uri("uri"));
            env.close();
            checkURI(issuer, "uri", __LINE__);

            // set URI Field
            env(token::modify(issuer, nftId), token::uri("new_uri"));
            env.close();
            checkURI(issuer, "new_uri", __LINE__);

            // unset URI Field
            env(token::modify(issuer, nftId));
            env.close();
            checkURI(issuer, nullptr, __LINE__);

            // set URI Field
            env(token::modify(issuer, nftId), token::uri("uri"));
            env.close();
            checkURI(issuer, "uri", __LINE__);

            // Account != Owner
            uint256 const offerID =
                keylet::nftoffer(issuer, env.seq(issuer)).key;
            env(token::createOffer(issuer, nftId, XRP(0)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(alice, offerID));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            checkURI(alice, "uri", __LINE__);

            // Modify by owner fails.
            env(token::modify(alice, nftId),
                token::uri("new_uri"),
                ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            checkURI(alice, "uri", __LINE__);

            env(token::modify(issuer, nftId),
                token::owner(alice),
                token::uri("new_uri"));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            checkURI(alice, "new_uri", __LINE__);

            env(token::modify(issuer, nftId), token::owner(alice));
            env.close();
            checkURI(alice, nullptr, __LINE__);

            env(token::modify(issuer, nftId),
                token::owner(alice),
                token::uri("uri"));
            env.close();
            checkURI(alice, "uri", __LINE__);

            // Modify by authorized minter
            env(token::setMinter(issuer, bob));
            env.close();
            env(token::modify(bob, nftId),
                token::owner(alice),
                token::uri("new_uri"));
            env.close();
            checkURI(alice, "new_uri", __LINE__);

            env(token::modify(bob, nftId), token::owner(alice));
            env.close();
            checkURI(alice, nullptr, __LINE__);

            env(token::modify(bob, nftId),
                token::owner(alice),
                token::uri("uri"));
            env.close();
            checkURI(alice, "uri", __LINE__);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnabled(features);
        testMintReserve(features);
        testMintMaxTokens(features);
        testMintInvalid(features);
        testBurnInvalid(features);
        testCreateOfferInvalid(features);
        testCancelOfferInvalid(features);
        testAcceptOfferInvalid(features);
        testMintFlagBurnable(features);
        testMintFlagOnlyXRP(features);
        testMintFlagCreateTrustLine(features);
        testMintFlagTransferable(features);
        testMintTransferFee(features);
        testMintTaxon(features);
        testMintURI(features);
        testCreateOfferDestination(features);
        testCreateOfferDestinationDisallowIncoming(features);
        testCreateOfferExpiration(features);
        testCancelOffers(features);
        testCancelTooManyOffers(features);
        testBrokeredAccept(features);
        testNFTokenOfferOwner(features);
        testNFTokenWithTickets(features);
        testNFTokenDeleteAccount(features);
        testNftXxxOffers(features);
        testFixNFTokenNegOffer(features);
        testIOUWithTransferFee(features);
        testBrokeredSaleToSelf(features);
        testFixNFTokenRemint(features);
        testFeatMintWithOffer(features);
        testTxJsonMetaFields(features);
        testFixNFTokenBuyerReserve(features);
        testUnaskedForAutoTrustline(features);
        testNFTIssuerIsIOUIssuer(features);
        testNFTokenModify(features);
    }

public:
    void
    run(std::uint32_t instance, bool last = false)
    {
        using namespace test::jtx;
        static FeatureBitset const all{testable_amendments()};
        static FeatureBitset const fixNFTDir{fixNFTokenDirV1};

        static std::array<FeatureBitset, 8> const feats{
            all - fixNFTDir - fixNonFungibleTokensV1_2 - fixNFTokenRemint -
                fixNFTokenReserve - featureNFTokenMintOffer - featureDynamicNFT,
            all - disallowIncoming - fixNonFungibleTokensV1_2 -
                fixNFTokenRemint - fixNFTokenReserve - featureNFTokenMintOffer -
                featureDynamicNFT,
            all - fixNonFungibleTokensV1_2 - fixNFTokenRemint -
                fixNFTokenReserve - featureNFTokenMintOffer - featureDynamicNFT,
            all - fixNFTokenRemint - fixNFTokenReserve -
                featureNFTokenMintOffer - featureDynamicNFT,
            all - fixNFTokenReserve - featureNFTokenMintOffer -
                featureDynamicNFT,
            all - featureNFTokenMintOffer - featureDynamicNFT,
            all - featureDynamicNFT,
            all};

        if (BEAST_EXPECT(instance < feats.size()))
        {
            testWithFeats(feats[instance]);
        }
        BEAST_EXPECT(!last || instance == feats.size() - 1);
    }

    void
    run() override
    {
        run(0);
    }
};

class NFTokenDisallowIncoming_test : public NFTokenBaseUtil_test
{
    void
    run() override
    {
        NFTokenBaseUtil_test::run(1);
    }
};

class NFTokenWOfixV1_test : public NFTokenBaseUtil_test
{
    void
    run() override
    {
        NFTokenBaseUtil_test::run(2);
    }
};

class NFTokenWOTokenRemint_test : public NFTokenBaseUtil_test
{
    void
    run() override
    {
        NFTokenBaseUtil_test::run(3);
    }
};

class NFTokenWOTokenReserve_test : public NFTokenBaseUtil_test
{
    void
    run() override
    {
        NFTokenBaseUtil_test::run(4);
    }
};

class NFTokenWOMintOffer_test : public NFTokenBaseUtil_test
{
    void
    run() override
    {
        NFTokenBaseUtil_test::run(5);
    }
};

class NFTokenWOModify_test : public NFTokenBaseUtil_test
{
    void
    run() override
    {
        NFTokenBaseUtil_test::run(6);
    }
};

class NFTokenAllFeatures_test : public NFTokenBaseUtil_test
{
    void
    run() override
    {
        NFTokenBaseUtil_test::run(7, true);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(NFTokenBaseUtil, tx, ripple, 2);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenDisallowIncoming, tx, ripple, 2);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenWOfixV1, tx, ripple, 2);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenWOTokenRemint, tx, ripple, 2);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenWOTokenReserve, tx, ripple, 2);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenWOMintOffer, tx, ripple, 2);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenWOModify, tx, ripple, 2);
BEAST_DEFINE_TESTSUITE_PRIO(NFTokenAllFeatures, tx, ripple, 2);

}  // namespace ripple
