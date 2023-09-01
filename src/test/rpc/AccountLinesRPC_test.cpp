//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

namespace RPC {

class AccountLinesRPC_test : public beast::unit_test::suite
{
public:
    void
    testAccountLines()
    {
        testcase("account_lines");

        using namespace test::jtx;
        Env env(*this);
        {
            // account_lines with no account.
            auto const lines = env.rpc("json", "account_lines", "{ }");
            BEAST_EXPECT(
                lines[jss::result][jss::error_message] ==
                RPC::missing_field_error(jss::account)[jss::error_message]);
        }
        {
            // account_lines with a malformed account.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": )"
                R"("n9MJkEKHDhy5eTLuHUQeAAjo382frHNbFK4C8hcwN4nwM2SrLdBj"})");
            BEAST_EXPECT(
                lines[jss::result][jss::error_message] ==
                RPC::make_error(rpcACT_MALFORMED)[jss::error_message]);
        }
        Account const alice{"alice"};
        {
            // account_lines on an unfunded account.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() + R"("})");
            BEAST_EXPECT(
                lines[jss::result][jss::error_message] ==
                RPC::make_error(rpcACT_NOT_FOUND)[jss::error_message]);
        }
        env.fund(XRP(10000), alice);
        env.close();
        LedgerInfo const ledger3Info = env.closed()->info();
        BEAST_EXPECT(ledger3Info.seq == 3);

        {
            // alice is funded but has no lines.  An empty array is returned.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() + R"("})");
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 0);
        }
        {
            // Specify a ledger that doesn't exist.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("ledger_index": "nonsense"})");
            BEAST_EXPECT(
                lines[jss::result][jss::error_message] ==
                "ledgerIndexMalformed");
        }
        {
            // Specify a different ledger that doesn't exist.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("ledger_index": 50000})");
            BEAST_EXPECT(
                lines[jss::result][jss::error_message] == "ledgerNotFound");
        }
        // Create trust lines to share with alice.
        Account const gw1{"gw1"};
        env.fund(XRP(10000), gw1);
        std::vector<IOU> gw1Currencies;

        for (char c = 0; c <= ('Z' - 'A'); ++c)
        {
            // gw1 currencies have names "YAA" -> "YAZ".
            gw1Currencies.push_back(
                gw1[std::string("YA") + static_cast<char>('A' + c)]);
            IOU const& gw1Currency = gw1Currencies.back();

            // Establish trust lines.
            env(trust(alice, gw1Currency(100 + c)));
            env(pay(gw1, alice, gw1Currency(50 + c)));
        }
        env.close();
        LedgerInfo const ledger4Info = env.closed()->info();
        BEAST_EXPECT(ledger4Info.seq == 4);

        // Add another set of trust lines in another ledger so we can see
        // differences in historic ledgers.
        Account const gw2{"gw2"};
        env.fund(XRP(10000), gw2);

        // gw2 requires authorization.
        env(fset(gw2, asfRequireAuth));
        env.close();
        std::vector<IOU> gw2Currencies;

        for (char c = 0; c <= ('Z' - 'A'); ++c)
        {
            // gw2 currencies have names "ZAA" -> "ZAZ".
            gw2Currencies.push_back(
                gw2[std::string("ZA") + static_cast<char>('A' + c)]);
            IOU const& gw2Currency = gw2Currencies.back();

            // Establish trust lines.
            env(trust(alice, gw2Currency(200 + c)));
            env(trust(gw2, gw2Currency(0), alice, tfSetfAuth));
            env.close();
            env(pay(gw2, alice, gw2Currency(100 + c)));
            env.close();

            // Set flags on gw2 trust lines so we can look for them.
            env(trust(alice, gw2Currency(0), gw2, tfSetNoRipple | tfSetFreeze));
        }
        env.close();
        LedgerInfo const ledger58Info = env.closed()->info();
        BEAST_EXPECT(ledger58Info.seq == 58);

        // A re-usable test for historic ledgers.
        auto testAccountLinesHistory = [this, &env](
                                           Account const& account,
                                           LedgerInfo const& info,
                                           int count) {
            // Get account_lines by ledger index.
            auto const linesSeq = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + account.human() +
                    R"(", )"
                    R"("ledger_index": )" +
                    std::to_string(info.seq) + "}");
            BEAST_EXPECT(linesSeq[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesSeq[jss::result][jss::lines].size() == count);

            // Get account_lines by ledger hash.
            auto const linesHash = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + account.human() +
                    R"(", )"
                    R"("ledger_hash": ")" +
                    to_string(info.hash) + R"("})");
            BEAST_EXPECT(linesHash[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesHash[jss::result][jss::lines].size() == count);
        };

        // Alice should have no trust lines in ledger 3.
        testAccountLinesHistory(alice, ledger3Info, 0);

        // Alice should have 26 trust lines in ledger 4.
        testAccountLinesHistory(alice, ledger4Info, 26);

        // Alice should have 52 trust lines in ledger 58.
        testAccountLinesHistory(alice, ledger58Info, 52);

        {
            // Surprisingly, it's valid to specify both index and hash, in
            // which case the hash wins.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("ledger_hash": ")" +
                    to_string(ledger4Info.hash) +
                    R"(", )"
                    R"("ledger_index": )" +
                    std::to_string(ledger58Info.seq) + "}");
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 26);
        }
        {
            // alice should have 52 trust lines in the current ledger.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() + R"("})");
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 52);
        }
        {
            // alice should have 26 trust lines with gw1.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("peer": ")" +
                    gw1.human() + R"("})");
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 26);
        }
        {
            // Use a malformed peer.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("peer": )"
                    R"("n9MJkEKHDhy5eTLuHUQeAAjo382frHNbFK4C8hcwN4nwM2SrLdBj"})");
            BEAST_EXPECT(
                lines[jss::result][jss::error_message] ==
                RPC::make_error(rpcACT_MALFORMED)[jss::error_message]);
        }
        {
            // A negative limit should fail.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("limit": -1})");
            BEAST_EXPECT(
                lines[jss::result][jss::error_message] ==
                RPC::expected_field_message(jss::limit, "unsigned integer"));
        }
        {
            // Limit the response to 1 trust line.
            auto const linesA = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("limit": 1})");
            BEAST_EXPECT(linesA[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesA[jss::result][jss::lines].size() == 1);

            // Pick up from where the marker left off.  We should get 51.
            auto marker = linesA[jss::result][jss::marker].asString();
            auto const linesB = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("marker": ")" +
                    marker + R"("})");
            BEAST_EXPECT(linesB[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesB[jss::result][jss::lines].size() == 51);

            // Go again from where the marker left off, but set a limit of 3.
            auto const linesC = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("limit": 3, )"
                    R"("marker": ")" +
                    marker + R"("})");
            BEAST_EXPECT(linesC[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesC[jss::result][jss::lines].size() == 3);

            // Mess with the marker so it becomes bad and check for the error.
            marker[5] = marker[5] == '7' ? '8' : '7';
            auto const linesD = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("marker": ")" +
                    marker + R"("})");
            BEAST_EXPECT(
                linesD[jss::result][jss::error_message] ==
                RPC::make_error(rpcINVALID_PARAMS)[jss::error_message]);
        }
        {
            // A non-string marker should also fail.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("marker": true})");
            BEAST_EXPECT(
                lines[jss::result][jss::error_message] ==
                RPC::expected_field_message(jss::marker, "string"));
        }
        {
            // Check that the flags we expect from alice to gw2 are present.
            auto const lines = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("limit": 10, )"
                    R"("peer": ")" +
                    gw2.human() + R"("})");
            auto const& line = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line[jss::freeze].asBool() == true);
            BEAST_EXPECT(line[jss::no_ripple].asBool() == true);
            BEAST_EXPECT(line[jss::peer_authorized].asBool() == true);
        }
        {
            // Check that the flags we expect from gw2 to alice are present.
            auto const linesA = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + gw2.human() +
                    R"(", )"
                    R"("limit": 1, )"
                    R"("peer": ")" +
                    alice.human() + R"("})");
            auto const& lineA = linesA[jss::result][jss::lines][0u];
            BEAST_EXPECT(lineA[jss::freeze_peer].asBool() == true);
            BEAST_EXPECT(lineA[jss::no_ripple_peer].asBool() == true);
            BEAST_EXPECT(lineA[jss::authorized].asBool() == true);

            // Continue from the returned marker to make sure that works.
            BEAST_EXPECT(linesA[jss::result].isMember(jss::marker));
            auto const marker = linesA[jss::result][jss::marker].asString();
            auto const linesB = env.rpc(
                "json",
                "account_lines",
                R"({"account": ")" + gw2.human() +
                    R"(", )"
                    R"("limit": 25, )"
                    R"("marker": ")" +
                    marker +
                    R"(", )"
                    R"("peer": ")" +
                    alice.human() + R"("})");
            BEAST_EXPECT(linesB[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesB[jss::result][jss::lines].size() == 25);
            BEAST_EXPECT(!linesB[jss::result].isMember(jss::marker));
        }
    }

    void
    testAccountLinesMarker()
    {
        testcase("Entry pointed to by marker is not owned by account");
        using namespace test::jtx;
        Env env(*this);

        // The goal of this test is observe account_lines RPC calls return an
        // error message when the SLE pointed to by the marker is not owned by
        // the Account being traversed.
        //
        // To start, we'll create an environment with some trust lines, offers
        // and a signers list.
        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const gw1{"gw1"};
        env.fund(XRP(10000), alice, becky, gw1);
        env.close();

        // Give alice a SignerList.
        Account const bogie{"bogie"};
        env(signers(alice, 2, {{bogie, 3}}));
        env.close();

        auto const EUR = gw1["EUR"];
        env(trust(alice, EUR(200)));
        env(trust(becky, EUR(200)));
        env.close();

        // Get all account objects for alice and verify that her
        // signerlist is first.  This is only a (reliable) coincidence of
        // object naming.  So if any of alice's objects are renamed this
        // may fail.
        Json::Value const aliceObjects = env.rpc(
            "json",
            "account_objects",
            R"({"account": ")" + alice.human() +
                R"(", )"
                R"("limit": 10})");
        Json::Value const& aliceSignerList =
            aliceObjects[jss::result][jss::account_objects][0u];
        if (!(aliceSignerList[sfLedgerEntryType.jsonName] == jss::SignerList))
        {
            fail(
                "alice's account objects are misordered.  "
                "Please reorder the objects so the SignerList is first.",
                __FILE__,
                __LINE__);
            return;
        }

        // Get account_lines for alice.  Limit at 1, so we get a marker
        // pointing to her SignerList.
        auto const aliceLines1 = env.rpc(
            "json",
            "account_lines",
            R"({"account": ")" + alice.human() + R"(", "limit": 1})");
        BEAST_EXPECT(aliceLines1[jss::result].isMember(jss::marker));

        // Verify that the marker points at the signer list.
        std::string const aliceMarker =
            aliceLines1[jss::result][jss::marker].asString();
        std::string const markerIndex =
            aliceMarker.substr(0, aliceMarker.find(','));
        BEAST_EXPECT(markerIndex == aliceSignerList[jss::index].asString());

        // When we fetch Alice's remaining lines we should find one and no more.
        auto const aliceLines2 = env.rpc(
            "json",
            "account_lines",
            R"({"account": ")" + alice.human() + R"(", "marker": ")" +
                aliceMarker + R"("})");
        BEAST_EXPECT(aliceLines2[jss::result][jss::lines].size() == 1);
        BEAST_EXPECT(!aliceLines2[jss::result].isMember(jss::marker));

        // Get account lines for beckys account, using alices SignerList as a
        // marker. This should cause an error.
        auto const beckyLines = env.rpc(
            "json",
            "account_lines",
            R"({"account": ")" + becky.human() + R"(", "marker": ")" +
                aliceMarker + R"("})");
        BEAST_EXPECT(beckyLines[jss::result].isMember(jss::error_message));
    }

    void
    testAccountLineDelete()
    {
        testcase("Entry pointed to by marker is removed");
        using namespace test::jtx;
        Env env(*this);

        // The goal here is to observe account_lines marker behavior if the
        // entry pointed at by a returned marker is removed from the ledger.
        //
        // It isn't easy to explicitly delete a trust line, so we do so in a
        // round-about fashion.  It takes 4 actors:
        //   o Gateway gw1 issues USD
        //   o alice offers to buy 100 USD for 100 XRP.
        //   o becky offers to sell 100 USD for 100 XRP.
        // There will now be an inferred trustline between alice and gw1.
        //   o alice pays her 100 USD to cheri.
        // alice should now have no USD and no trustline to gw1.
        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const cheri{"cheri"};
        Account const gw1{"gw1"};
        Account const gw2{"gw2"};
        env.fund(XRP(10000), alice, becky, cheri, gw1, gw2);
        env.close();

        auto const USD = gw1["USD"];
        auto const AUD = gw1["AUD"];
        auto const EUR = gw2["EUR"];
        env(trust(alice, USD(200)));
        env(trust(alice, AUD(200)));
        env(trust(becky, EUR(200)));
        env(trust(cheri, EUR(200)));
        env.close();

        // becky gets 100 USD from gw1.
        env(pay(gw2, becky, EUR(100)));
        env.close();

        // alice offers to buy 100 EUR for 100 XRP.
        env(offer(alice, EUR(100), XRP(100)));
        env.close();

        // becky offers to buy 100 XRP for 100 EUR.
        env(offer(becky, XRP(100), EUR(100)));
        env.close();

        // Get account_lines for alice.  Limit at 1, so we get a marker.
        auto const linesBeg = env.rpc(
            "json",
            "account_lines",
            R"({"account": ")" + alice.human() +
                R"(", )"
                R"("limit": 2})");
        BEAST_EXPECT(
            linesBeg[jss::result][jss::lines][0u][jss::currency] == "USD");
        BEAST_EXPECT(linesBeg[jss::result].isMember(jss::marker));

        // alice pays 100 EUR to cheri.
        env(pay(alice, cheri, EUR(100)));
        env.close();

        // Since alice paid all her EUR to cheri, alice should no longer
        // have a trust line to gw1.  So the old marker should now be invalid.
        auto const linesEnd = env.rpc(
            "json",
            "account_lines",
            R"({"account": ")" + alice.human() +
                R"(", )"
                R"("marker": ")" +
                linesBeg[jss::result][jss::marker].asString() + R"("})");
        BEAST_EXPECT(
            linesEnd[jss::result][jss::error_message] ==
            RPC::make_error(rpcINVALID_PARAMS)[jss::error_message]);
    }

    void
    testAccountLinesWalkMarkers()
    {
        testcase("Marker can point to any appropriate ledger entry type");
        using namespace test::jtx;
        using namespace std::chrono_literals;
        Env env(*this);

        // The goal of this test is observe account_lines RPC calls return an
        // error message when the SLE pointed to by the marker is not owned by
        // the Account being traversed.
        //
        // To start, we'll create an environment with some trust lines, offers
        // and a signers list.
        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const gw1{"gw1"};
        env.fund(XRP(10000), alice, becky, gw1);
        env.close();

        // A couple of helper lambdas
        auto escrow = [&env](
                          Account const& account,
                          Account const& to,
                          STAmount const& amount) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::EscrowCreate;
            jv[jss::Flags] = tfUniversal;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            NetClock::time_point finish = env.now() + 1s;
            jv[sfFinishAfter.jsonName] = finish.time_since_epoch().count();
            return jv;
        };

        auto payChan = [](Account const& account,
                          Account const& to,
                          STAmount const& amount,
                          NetClock::duration const& settleDelay,
                          PublicKey const& pk) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::PaymentChannelCreate;
            jv[jss::Flags] = tfUniversal;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv["SettleDelay"] = settleDelay.count();
            jv["PublicKey"] = strHex(pk.slice());
            return jv;
        };

        // Test all available object types. Not all of these objects will be
        // included in the search, nor found by `account_objects`. If that ever
        // changes for any reason, this test will help catch that.
        //
        // SignerList, for alice
        Account const bogie{"bogie"};
        env(signers(alice, 2, {{bogie, 3}}));
        env.close();

        // SignerList, includes alice
        env(signers(becky, 2, {{alice, 3}}));
        env.close();

        // Trust lines
        auto const EUR = gw1["EUR"];
        env(trust(alice, EUR(200)));
        env(trust(becky, EUR(200)));
        env.close();

        // Escrow, in each direction
        env(escrow(alice, becky, XRP(1000)));
        env(escrow(becky, alice, XRP(1000)));

        // Pay channels, in each direction
        env(payChan(alice, becky, XRP(1000), 100s, alice.pk()));
        env(payChan(becky, alice, XRP(1000), 100s, becky.pk()));

        // Mint NFTs, for each account
        uint256 const aliceNFtokenID =
            token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0), txflags(tfTransferable));

        uint256 const beckyNFtokenID =
            token::getNextID(env, becky, 0, tfTransferable);
        env(token::mint(becky, 0), txflags(tfTransferable));

        // NFT Offers, for each other's NFTs
        env(token::createOffer(alice, beckyNFtokenID, drops(1)),
            token::owner(becky));
        env(token::createOffer(becky, aliceNFtokenID, drops(1)),
            token::owner(alice));

        env(token::createOffer(becky, beckyNFtokenID, drops(1)),
            txflags(tfSellNFToken),
            token::destination(alice));
        env(token::createOffer(alice, aliceNFtokenID, drops(1)),
            txflags(tfSellNFToken),
            token::destination(becky));

        env(token::createOffer(gw1, beckyNFtokenID, drops(1)),
            token::owner(becky),
            token::destination(alice));
        env(token::createOffer(gw1, aliceNFtokenID, drops(1)),
            token::owner(alice),
            token::destination(becky));

        env(token::createOffer(becky, beckyNFtokenID, drops(1)),
            txflags(tfSellNFToken));
        env(token::createOffer(alice, aliceNFtokenID, drops(1)),
            txflags(tfSellNFToken));

        // Checks, in each direction
        env(check::create(alice, becky, XRP(50)));
        env(check::create(becky, alice, XRP(50)));

        // Deposit preauth, in each direction
        env(deposit::auth(alice, becky));
        env(deposit::auth(becky, alice));

        // Offers, one where alice is the owner, and one where alice is the
        // issuer
        auto const USDalice = alice["USD"];
        env(offer(alice, EUR(10), XRP(100)));
        env(offer(becky, USDalice(10), XRP(100)));

        // Tickets
        env(ticket::create(alice, 2));

        // Add another trustline for good measure
        auto const BTCbecky = becky["BTC"];
        env(trust(alice, BTCbecky(200)));

        env.close();

        {
            // Now make repeated calls to `account_lines` with a limit of 1.
            // That should iterate all of alice's relevant objects, even though
            // the list will be empty for most calls.
            auto getNextLine = [](Env& env,
                                  Account const& alice,
                                  std::optional<std::string> const marker) {
                Json::Value params(Json::objectValue);
                params[jss::account] = alice.human();
                params[jss::limit] = 1;
                if (marker)
                    params[jss::marker] = *marker;

                return env.rpc("json", "account_lines", to_string(params));
            };

            auto aliceLines = getNextLine(env, alice, std::nullopt);
            constexpr std::size_t expectedIterations = 16;
            constexpr std::size_t expectedLines = 2;
            constexpr std::size_t expectedNFTs = 1;
            std::size_t foundLines = 0;

            auto hasMarker = [](auto const& aliceLines) {
                return aliceLines[jss::result].isMember(jss::marker);
            };
            auto marker = [](auto const& aliceLines) {
                return aliceLines[jss::result][jss::marker].asString();
            };
            auto checkLines = [](auto const& aliceLines) {
                return aliceLines.isMember(jss::result) &&
                    !aliceLines[jss::result].isMember(jss::error_message) &&
                    aliceLines[jss::result].isMember(jss::lines) &&
                    aliceLines[jss::result][jss::lines].isArray() &&
                    aliceLines[jss::result][jss::lines].size() <= 1;
            };

            BEAST_EXPECT(hasMarker(aliceLines));
            BEAST_EXPECT(checkLines(aliceLines));
            BEAST_EXPECT(aliceLines[jss::result][jss::lines].size() == 0);

            int iterations = 1;

            while (hasMarker(aliceLines))
            {
                // Iterate through the markers
                aliceLines = getNextLine(env, alice, marker(aliceLines));
                BEAST_EXPECT(checkLines(aliceLines));
                foundLines += aliceLines[jss::result][jss::lines].size();
                ++iterations;
            }
            BEAST_EXPECT(expectedLines == foundLines);

            Json::Value const aliceObjects = env.rpc(
                "json",
                "account_objects",
                R"({"account": ")" + alice.human() +
                    R"(", )"
                    R"("limit": 200})");
            BEAST_EXPECT(aliceObjects.isMember(jss::result));
            BEAST_EXPECT(
                !aliceObjects[jss::result].isMember(jss::error_message));
            BEAST_EXPECT(
                aliceObjects[jss::result].isMember(jss::account_objects));
            BEAST_EXPECT(
                aliceObjects[jss::result][jss::account_objects].isArray());
            // account_objects does not currently return NFTPages. If
            // that ever changes, without also changing account_lines,
            // this test will need to be updated.
            BEAST_EXPECT(
                aliceObjects[jss::result][jss::account_objects].size() ==
                iterations + expectedNFTs);
            // If ledger object association ever changes, for whatever
            // reason, this test will need to be updated.
            BEAST_EXPECTS(
                iterations == expectedIterations, std::to_string(iterations));

            // Get becky's objects just to confirm that they're symmetrical
            Json::Value const beckyObjects = env.rpc(
                "json",
                "account_objects",
                R"({"account": ")" + becky.human() +
                    R"(", )"
                    R"("limit": 200})");
            BEAST_EXPECT(beckyObjects.isMember(jss::result));
            BEAST_EXPECT(
                !beckyObjects[jss::result].isMember(jss::error_message));
            BEAST_EXPECT(
                beckyObjects[jss::result].isMember(jss::account_objects));
            BEAST_EXPECT(
                beckyObjects[jss::result][jss::account_objects].isArray());
            // becky should have the same number of objects as alice, except the
            // 2 tickets that only alice created.
            BEAST_EXPECT(
                beckyObjects[jss::result][jss::account_objects].size() ==
                aliceObjects[jss::result][jss::account_objects].size() - 2);
        }
    }

    // test API V2
    void
    testAccountLines2()
    {
        testcase("V2: account_lines");

        using namespace test::jtx;
        Env env(*this);
        {
            // account_lines with mal-formed json2 (missing id field).
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0")"
                " }");
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
        }
        {
            // account_lines with no account.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5)"
                " }");
            BEAST_EXPECT(
                lines[jss::error][jss::message] ==
                RPC::missing_field_error(jss::account)[jss::error_message]);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // account_lines with a malformed account.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": )"
                R"("n9MJkEKHDhy5eTLuHUQeAAjo382frHNbFK4C8hcwN4nwM2SrLdBj"}})");
            BEAST_EXPECT(
                lines[jss::error][jss::message] ==
                RPC::make_error(rpcACT_MALFORMED)[jss::error_message]);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        Account const alice{"alice"};
        {
            // account_lines on an unfunded account.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() + R"("}})");
            BEAST_EXPECT(
                lines[jss::error][jss::message] ==
                RPC::make_error(rpcACT_NOT_FOUND)[jss::error_message]);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        env.fund(XRP(10000), alice);
        env.close();
        LedgerInfo const ledger3Info = env.closed()->info();
        BEAST_EXPECT(ledger3Info.seq == 3);

        {
            // alice is funded but has no lines.  An empty array is returned.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() + R"("}})");
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 0);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // Specify a ledger that doesn't exist.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("ledger_index": "nonsense"}})");
            BEAST_EXPECT(
                lines[jss::error][jss::message] == "ledgerIndexMalformed");
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // Specify a different ledger that doesn't exist.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("ledger_index": 50000}})");
            BEAST_EXPECT(lines[jss::error][jss::message] == "ledgerNotFound");
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        // Create trust lines to share with alice.
        Account const gw1{"gw1"};
        env.fund(XRP(10000), gw1);
        std::vector<IOU> gw1Currencies;

        for (char c = 0; c <= ('Z' - 'A'); ++c)
        {
            // gw1 currencies have names "YAA" -> "YAZ".
            gw1Currencies.push_back(
                gw1[std::string("YA") + static_cast<char>('A' + c)]);
            IOU const& gw1Currency = gw1Currencies.back();

            // Establish trust lines.
            env(trust(alice, gw1Currency(100 + c)));
            env(pay(gw1, alice, gw1Currency(50 + c)));
        }
        env.close();
        LedgerInfo const ledger4Info = env.closed()->info();
        BEAST_EXPECT(ledger4Info.seq == 4);

        // Add another set of trust lines in another ledger so we can see
        // differences in historic ledgers.
        Account const gw2{"gw2"};
        env.fund(XRP(10000), gw2);

        // gw2 requires authorization.
        env(fset(gw2, asfRequireAuth));
        env.close();
        std::vector<IOU> gw2Currencies;

        for (char c = 0; c <= ('Z' - 'A'); ++c)
        {
            // gw2 currencies have names "ZAA" -> "ZAZ".
            gw2Currencies.push_back(
                gw2[std::string("ZA") + static_cast<char>('A' + c)]);
            IOU const& gw2Currency = gw2Currencies.back();

            // Establish trust lines.
            env(trust(alice, gw2Currency(200 + c)));
            env(trust(gw2, gw2Currency(0), alice, tfSetfAuth));
            env.close();
            env(pay(gw2, alice, gw2Currency(100 + c)));
            env.close();

            // Set flags on gw2 trust lines so we can look for them.
            env(trust(alice, gw2Currency(0), gw2, tfSetNoRipple | tfSetFreeze));
        }
        env.close();
        LedgerInfo const ledger58Info = env.closed()->info();
        BEAST_EXPECT(ledger58Info.seq == 58);

        // A re-usable test for historic ledgers.
        auto testAccountLinesHistory = [this, &env](
                                           Account const& account,
                                           LedgerInfo const& info,
                                           int count) {
            // Get account_lines by ledger index.
            auto const linesSeq = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    account.human() +
                    R"(", )"
                    R"("ledger_index": )" +
                    std::to_string(info.seq) + "}}");
            BEAST_EXPECT(linesSeq[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesSeq[jss::result][jss::lines].size() == count);
            BEAST_EXPECT(
                linesSeq.isMember(jss::jsonrpc) &&
                linesSeq[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                linesSeq.isMember(jss::ripplerpc) &&
                linesSeq[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(linesSeq.isMember(jss::id) && linesSeq[jss::id] == 5);

            // Get account_lines by ledger hash.
            auto const linesHash = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    account.human() +
                    R"(", )"
                    R"("ledger_hash": ")" +
                    to_string(info.hash) + R"("}})");
            BEAST_EXPECT(linesHash[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesHash[jss::result][jss::lines].size() == count);
            BEAST_EXPECT(
                linesHash.isMember(jss::jsonrpc) &&
                linesHash[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                linesHash.isMember(jss::ripplerpc) &&
                linesHash[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(
                linesHash.isMember(jss::id) && linesHash[jss::id] == 5);
        };

        // Alice should have no trust lines in ledger 3.
        testAccountLinesHistory(alice, ledger3Info, 0);

        // Alice should have 26 trust lines in ledger 4.
        testAccountLinesHistory(alice, ledger4Info, 26);

        // Alice should have 52 trust lines in ledger 58.
        testAccountLinesHistory(alice, ledger58Info, 52);

        {
            // Surprisingly, it's valid to specify both index and hash, in
            // which case the hash wins.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("ledger_hash": ")" +
                    to_string(ledger4Info.hash) +
                    R"(", )"
                    R"("ledger_index": )" +
                    std::to_string(ledger58Info.seq) + "}}");
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 26);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // alice should have 52 trust lines in the current ledger.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() + R"("}})");
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 52);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // alice should have 26 trust lines with gw1.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("peer": ")" +
                    gw1.human() + R"("}})");
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 26);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // Use a malformed peer.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("peer": )"
                    R"("n9MJkEKHDhy5eTLuHUQeAAjo382frHNbFK4C8hcwN4nwM2SrLdBj"}})");
            BEAST_EXPECT(
                lines[jss::error][jss::message] ==
                RPC::make_error(rpcACT_MALFORMED)[jss::error_message]);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // A negative limit should fail.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("limit": -1}})");
            BEAST_EXPECT(
                lines[jss::error][jss::message] ==
                RPC::expected_field_message(jss::limit, "unsigned integer"));
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // Limit the response to 1 trust line.
            auto const linesA = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("limit": 1}})");
            BEAST_EXPECT(linesA[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesA[jss::result][jss::lines].size() == 1);
            BEAST_EXPECT(
                linesA.isMember(jss::jsonrpc) && linesA[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                linesA.isMember(jss::ripplerpc) &&
                linesA[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(linesA.isMember(jss::id) && linesA[jss::id] == 5);

            // Pick up from where the marker left off.  We should get 51.
            auto marker = linesA[jss::result][jss::marker].asString();
            auto const linesB = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("marker": ")" +
                    marker + R"("}})");
            BEAST_EXPECT(linesB[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesB[jss::result][jss::lines].size() == 51);
            BEAST_EXPECT(
                linesB.isMember(jss::jsonrpc) && linesB[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                linesB.isMember(jss::ripplerpc) &&
                linesB[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(linesB.isMember(jss::id) && linesB[jss::id] == 5);

            // Go again from where the marker left off, but set a limit of 3.
            auto const linesC = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("limit": 3, )"
                    R"("marker": ")" +
                    marker + R"("}})");
            BEAST_EXPECT(linesC[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesC[jss::result][jss::lines].size() == 3);
            BEAST_EXPECT(
                linesC.isMember(jss::jsonrpc) && linesC[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                linesC.isMember(jss::ripplerpc) &&
                linesC[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(linesC.isMember(jss::id) && linesC[jss::id] == 5);

            // Mess with the marker so it becomes bad and check for the error.
            marker[5] = marker[5] == '7' ? '8' : '7';
            auto const linesD = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("marker": ")" +
                    marker + R"("}})");
            BEAST_EXPECT(
                linesD[jss::error][jss::message] ==
                RPC::make_error(rpcINVALID_PARAMS)[jss::error_message]);
            BEAST_EXPECT(
                linesD.isMember(jss::jsonrpc) && linesD[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                linesD.isMember(jss::ripplerpc) &&
                linesD[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(linesD.isMember(jss::id) && linesD[jss::id] == 5);
        }
        {
            // A non-string marker should also fail.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("marker": true}})");
            BEAST_EXPECT(
                lines[jss::error][jss::message] ==
                RPC::expected_field_message(jss::marker, "string"));
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // Check that the flags we expect from alice to gw2 are present.
            auto const lines = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    alice.human() +
                    R"(", )"
                    R"("limit": 10, )"
                    R"("peer": ")" +
                    gw2.human() + R"("}})");
            auto const& line = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line[jss::freeze].asBool() == true);
            BEAST_EXPECT(line[jss::no_ripple].asBool() == true);
            BEAST_EXPECT(line[jss::peer_authorized].asBool() == true);
            BEAST_EXPECT(
                lines.isMember(jss::jsonrpc) && lines[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                lines.isMember(jss::ripplerpc) &&
                lines[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(lines.isMember(jss::id) && lines[jss::id] == 5);
        }
        {
            // Check that the flags we expect from gw2 to alice are present.
            auto const linesA = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    gw2.human() +
                    R"(", )"
                    R"("limit": 1, )"
                    R"("peer": ")" +
                    alice.human() + R"("}})");
            auto const& lineA = linesA[jss::result][jss::lines][0u];
            BEAST_EXPECT(lineA[jss::freeze_peer].asBool() == true);
            BEAST_EXPECT(lineA[jss::no_ripple_peer].asBool() == true);
            BEAST_EXPECT(lineA[jss::authorized].asBool() == true);
            BEAST_EXPECT(
                linesA.isMember(jss::jsonrpc) && linesA[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                linesA.isMember(jss::ripplerpc) &&
                linesA[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(linesA.isMember(jss::id) && linesA[jss::id] == 5);

            // Continue from the returned marker to make sure that works.
            BEAST_EXPECT(linesA[jss::result].isMember(jss::marker));
            auto const marker = linesA[jss::result][jss::marker].asString();
            auto const linesB = env.rpc(
                "json2",
                "{ "
                R"("method" : "account_lines",)"
                R"("jsonrpc" : "2.0",)"
                R"("ripplerpc" : "2.0",)"
                R"("id" : 5,)"
                R"("params": )"
                R"({"account": ")" +
                    gw2.human() +
                    R"(", )"
                    R"("limit": 25, )"
                    R"("marker": ")" +
                    marker +
                    R"(", )"
                    R"("peer": ")" +
                    alice.human() + R"("}})");
            BEAST_EXPECT(linesB[jss::result][jss::lines].isArray());
            BEAST_EXPECT(linesB[jss::result][jss::lines].size() == 25);
            BEAST_EXPECT(!linesB[jss::result].isMember(jss::marker));
            BEAST_EXPECT(
                linesB.isMember(jss::jsonrpc) && linesB[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                linesB.isMember(jss::ripplerpc) &&
                linesB[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(linesB.isMember(jss::id) && linesB[jss::id] == 5);
        }
    }

    // test API V2
    void
    testAccountLineDelete2()
    {
        testcase("V2: account_lines with removed marker");

        using namespace test::jtx;
        Env env(*this);

        // The goal here is to observe account_lines marker behavior if the
        // entry pointed at by a returned marker is removed from the ledger.
        //
        // It isn't easy to explicitly delete a trust line, so we do so in a
        // round-about fashion.  It takes 4 actors:
        //   o Gateway gw1 issues EUR
        //   o alice offers to buy 100 EUR for 100 XRP.
        //   o becky offers to sell 100 EUR for 100 XRP.
        // There will now be an inferred trustline between alice and gw2.
        //   o alice pays her 100 EUR to cheri.
        // alice should now have no EUR and no trustline to gw2.
        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const cheri{"cheri"};
        Account const gw1{"gw1"};
        Account const gw2{"gw2"};
        env.fund(XRP(10000), alice, becky, cheri, gw1, gw2);
        env.close();

        auto const USD = gw1["USD"];
        auto const AUD = gw1["AUD"];
        auto const EUR = gw2["EUR"];
        env(trust(alice, USD(200)));
        env(trust(alice, AUD(200)));
        env(trust(becky, EUR(200)));
        env(trust(cheri, EUR(200)));
        env.close();

        // becky gets 100 EUR from gw1.
        env(pay(gw2, becky, EUR(100)));
        env.close();

        // alice offers to buy 100 EUR for 100 XRP.
        env(offer(alice, EUR(100), XRP(100)));
        env.close();

        // becky offers to buy 100 XRP for 100 EUR.
        env(offer(becky, XRP(100), EUR(100)));
        env.close();

        // Get account_lines for alice.  Limit at 1, so we get a marker.
        auto const linesBeg = env.rpc(
            "json2",
            "{ "
            R"("method" : "account_lines",)"
            R"("jsonrpc" : "2.0",)"
            R"("ripplerpc" : "2.0",)"
            R"("id" : 5,)"
            R"("params": )"
            R"({"account": ")" +
                alice.human() +
                R"(", )"
                R"("limit": 2}})");
        BEAST_EXPECT(
            linesBeg[jss::result][jss::lines][0u][jss::currency] == "USD");
        BEAST_EXPECT(linesBeg[jss::result].isMember(jss::marker));
        BEAST_EXPECT(
            linesBeg.isMember(jss::jsonrpc) && linesBeg[jss::jsonrpc] == "2.0");
        BEAST_EXPECT(
            linesBeg.isMember(jss::ripplerpc) &&
            linesBeg[jss::ripplerpc] == "2.0");
        BEAST_EXPECT(linesBeg.isMember(jss::id) && linesBeg[jss::id] == 5);

        // alice pays 100 USD to cheri.
        env(pay(alice, cheri, EUR(100)));
        env.close();

        // Since alice paid all her EUR to cheri, alice should no longer
        // have a trust line to gw1.  So the old marker should now be invalid.
        auto const linesEnd = env.rpc(
            "json2",
            "{ "
            R"("method" : "account_lines",)"
            R"("jsonrpc" : "2.0",)"
            R"("ripplerpc" : "2.0",)"
            R"("id" : 5,)"
            R"("params": )"
            R"({"account": ")" +
                alice.human() +
                R"(", )"
                R"("marker": ")" +
                linesBeg[jss::result][jss::marker].asString() + R"("}})");
        BEAST_EXPECT(
            linesEnd[jss::error][jss::message] ==
            RPC::make_error(rpcINVALID_PARAMS)[jss::error_message]);
        BEAST_EXPECT(
            linesEnd.isMember(jss::jsonrpc) && linesEnd[jss::jsonrpc] == "2.0");
        BEAST_EXPECT(
            linesEnd.isMember(jss::ripplerpc) &&
            linesEnd[jss::ripplerpc] == "2.0");
        BEAST_EXPECT(linesEnd.isMember(jss::id) && linesEnd[jss::id] == 5);
    }

    void
    run() override
    {
        testAccountLines();
        testAccountLinesMarker();
        testAccountLineDelete();
        testAccountLinesWalkMarkers();
        testAccountLines2();
        testAccountLineDelete2();
    }
};

BEAST_DEFINE_TESTSUITE(AccountLinesRPC, app, ripple);

}  // namespace RPC
}  // namespace ripple
