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

#include <ripple/protocol/JsonFields.h>
#include <ripple/json/json_value.h>
#include <ripple/json/to_string.h>
#include <ripple/json/json_reader.h>
#include <test/jtx.h>

#include <boost/utility/string_ref.hpp>

namespace ripple {
namespace test {

static char const* bobs_account_objects[] = {
R"json({
  "Account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
  "BookDirectory" : "50AD0A9E54D2B381288D535EB724E4275FFBF41580D28A925D038D7EA4C68000",
  "BookNode" : "0000000000000000",
  "Flags" : 65536,
  "LedgerEntryType" : "Offer",
  "OwnerNode" : "0000000000000000",
  "Sequence" : 4,
  "TakerGets" : {
    "currency" : "USD",
    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
    "value" : "1"
  },
  "TakerPays" : "100000000",
  "index" : "A984D036A0E562433A8377CA57D1A1E056E58C0D04818F8DFD3A1AA3F217DD82"
})json"
,
R"json({
    "Account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
    "BookDirectory" : "B025997A323F5C3E03DDF1334471F5984ABDE31C59D463525D038D7EA4C68000",
    "BookNode" : "0000000000000000",
    "Flags" : 65536,
    "LedgerEntryType" : "Offer",
    "OwnerNode" : "0000000000000000",
    "Sequence" : 5,
    "TakerGets" : {
        "currency" : "USD",
        "issuer" : "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
        "value" : "1"
    },
    "TakerPays" : "100000000",
    "index" : "CAFE32332D752387B01083B60CC63069BA4A969C9730836929F841450F6A718E"
})json"
,
R"json({
    "Balance" : {
        "currency" : "USD",
        "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
        "value" : "-1000"
    },
    "Flags" : 131072,
    "HighLimit" : {
        "currency" : "USD",
        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value" : "1000"
    },
    "HighNode" : "0000000000000000",
    "LedgerEntryType" : "RippleState",
    "LowLimit" : {
        "currency" : "USD",
        "issuer" : "r9cZvwKU3zzuZK9JFovGg1JC5n7QiqNL8L",
        "value" : "0"
    },
    "LowNode" : "0000000000000000",
    "index" : "D13183BCFFC9AAC9F96AEBB5F66E4A652AD1F5D10273AEB615478302BEBFD4A4"
})json"
,
R"json({
    "Balance" : {
        "currency" : "USD",
        "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
        "value" : "-1000"
    },
    "Flags" : 131072,
    "HighLimit" : {
        "currency" : "USD",
        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value" : "1000"
    },
    "HighNode" : "0000000000000000",
    "LedgerEntryType" : "RippleState",
    "LowLimit" : {
        "currency" : "USD",
        "issuer" : "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
        "value" : "0"
    },
    "LowNode" : "0000000000000000",
    "index" : "D89BC239086183EB9458C396E643795C1134963E6550E682A190A5F021766D43"
})json"
};

class AccountObjects_test : public beast::unit_test::suite
{
public:
    void testErrors()
    {
        testcase("error cases");

        using namespace jtx;
        Env env(*this);

        // test error on no account
        {
            auto resp = env.rpc("json", "account_objects");
            BEAST_EXPECT( resp[jss::error_message] ==
                "Syntax error.");
        }
        // test error on  malformed account string.
        {
            Json::Value params;
            params[jss::account] = "n94JNrQYkDrpt62bbSR7nVEhdyAvcJXRAsjEkFYyqRkh9SUTYEqV";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Disallowed seed.");
        }
        // test error on account that's not in the ledger.
        {
            Json::Value params;
            params[jss::account] = Account{ "bogie" }.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Account not found.");
        }
        Account const bob{ "bob" };
        // test error on large ledger_index.
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::ledger_index] = 10;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "ledgerNotFound");
        }

        env.fund(XRP(1000), bob);
        // test error on type param not a string
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = 10;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] ==
                "Invalid field 'type', not string.");
        }
        // test error on type param not a valid type
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = "expedited";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Invalid field 'type'.");
        }
        // test error on limit -ve
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = -1;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] ==
                "Invalid field 'limit', not unsigned integer.");
        }
       // test errors on marker
        {
            Account const gw{ "G" };
            env.fund(XRP(1000), gw);
            auto const USD = gw["USD"];
            env.trust(USD(1000), bob);
            env(pay(gw, bob, XRP(1)));
            env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));

            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            auto resp = env.rpc("json", "account_objects", to_string(params));

            auto resume_marker = resp[jss::result][jss::marker];
            std::string mark = to_string(resume_marker);
            params[jss::marker] = 10;
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Invalid field 'marker', not string.");

            params[jss::marker] = "This is a string with no comma";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Invalid field 'marker'.");

            params[jss::marker] = "This string has a comma, but is not hex";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 64);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 65);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 65) + "not hex";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::error_message] == "Invalid field 'marker'.");

            // Should this be an error?
            // A hex digit is absent from the end of marker.
            // No account objects returned.
            params[jss::marker] = std::string(&mark[1U], 128);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( resp[jss::result][jss::account_objects].size() == 0);
        }
    }

    void testUnsteppedThenStepped()
    {
        testcase("unsteppedThenStepped");

        using namespace jtx;
        Env env(*this);

        Account const gw1{ "G1" };
        Account const gw2{ "G2" };
        Account const bob{ "bob" };

        auto const USD1 = gw1["USD"];
        auto const USD2 = gw2["USD"];

        env.fund(XRP(1000), gw1, gw2, bob);
        env.trust(USD1(1000), bob);
        env.trust(USD2(1000), bob);

        env(pay(gw1, bob, USD1(1000)));
        env(pay(gw2, bob, USD2(1000)));

        env(offer(bob, XRP(100), bob["USD"](1)),txflags(tfPassive));
        env(offer(bob, XRP(100), USD1(1)), txflags(tfPassive));

        Json::Value bobj[4];
        for (int i = 0; i < 4; ++i)
            Json::Reader{}.parse(bobs_account_objects[i], bobj[i]);

        // test 'unstepped'
        // i.e. request account objects without explicit limit/marker paging
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( !resp.isMember(jss::marker));

            BEAST_EXPECT( resp[jss::result][jss::account_objects].size() == 4);
            for (int i = 0; i < 4; ++i)
            {
                auto& aobj = resp[jss::result][jss::account_objects][i];
                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");

                if (aobj != bobj[i])
                   std::cout << "Fail at " << i << ": " << aobj << std::endl;
                BEAST_EXPECT(aobj == bobj[i]);
            }
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = "state";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT( !resp.isMember(jss::marker));

            BEAST_EXPECT( resp[jss::result][jss::account_objects].size() == 2);
            for (int i = 0; i < 2; ++i)
            {
                auto& aobj = resp[jss::result][jss::account_objects][i];
                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");

                BEAST_EXPECT( aobj == bobj[i+2]);
            }
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            for (int i = 0; i < 4; ++i)
            {
                auto resp = env.rpc("json", "account_objects", to_string(params));
                auto& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT( aobjs.size() == 1);
                auto& aobj = aobjs[0U];
                if (i < 3) BEAST_EXPECT( resp[jss::result][jss::limit] == 1);

                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");

                BEAST_EXPECT(aobj == bobj[i]);

                auto resume_marker = resp[jss::result][jss::marker];
                params[jss::marker] = resume_marker;
            }
        }
    }

    void testObjectTypes()
    {
        testcase ("object types");

        // Give gw a bunch of ledger objects and make sure we can retrieve
        // them by type.
        using namespace jtx;

        Account const alice { "alice" };
        Account const gw{ "gateway" };
        auto const USD = gw["USD"];

        // Test for ticket account objects when they are supported.
        Env env(*this, supported_amendments().set(featureTickets));

        // Make a lambda we can use to get "account_objects" easily.
        auto acct_objs = [&env] (Account const& acct, char const* type)
        {
            Json::Value params;
            params[jss::account] = acct.human();
            params[jss::type] = type;
            params[jss::ledger_index] = "validated";
            return env.rpc("json", "account_objects", to_string(params));
        };

        // Make a lambda that easily identifies empty account objects.
        auto acct_objs_is_empty =
            [&env, &acct_objs] (Account const& acct, char const* type)
        {
            Json::Value const resp = acct_objs (acct, type);

            bool const isArray = resp["result"]["account_objects"].isArray();
            return isArray && (resp["result"]["account_objects"].size() == 0);
        };

        env.fund(XRP(10000), gw, alice);
        env.close();

        // Since the account is empty now, all account objects should come
        // back null or empty.
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::account));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::amendments));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::check));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::directory));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::escrow));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::fee));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::hashes));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::offer));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::payment_channel));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::signer_list));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::state));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::ticket));

        // Set up a trust line so we can find it.
        env.trust(USD(1000), alice);
        env.close();
        env(pay(gw, alice, USD(5)));
        env.close();
        {
            // Find the trustline and make sure it's the right one.
            Json::Value const resp = acct_objs (gw, jss::state);

            BEAST_EXPECT (resp["result"]["account_objects"].isArray());
            BEAST_EXPECT (resp["result"]["account_objects"].size() == 1);

            auto const& state = resp["result"]["account_objects"][0u];
            BEAST_EXPECT (state["Balance"]["value"].asInt() == -5);
            BEAST_EXPECT (state["HighLimit"]["value"].asInt() == 1000);
        }
        {
            // gw writes a check for USD(10) to alice.
            Json::Value jvCheck;
            jvCheck[sfAccount.jsonName] = gw.human();
            jvCheck[sfSendMax.jsonName] = USD(10).value().getJson(0);
            jvCheck[sfDestination.jsonName] = alice.human();
            jvCheck[sfTransactionType.jsonName] = "CheckCreate";
            jvCheck[sfFlags.jsonName] = tfUniversal;
            env (jvCheck);
            env.close();
        }
        {
            // Find the check.
            Json::Value const resp = acct_objs (gw, jss::check);

            BEAST_EXPECT (resp["result"]["account_objects"].isArray());
            BEAST_EXPECT (resp["result"]["account_objects"].size() == 1);

            auto const& check = resp["result"]["account_objects"][0u];
            BEAST_EXPECT (check["Account"] == gw.human());
            BEAST_EXPECT (check["Destination"] == alice.human());
            BEAST_EXPECT (check["SendMax"]["value"].asInt() == 10);
        }
        {
            // gw creates an escrow that we can look for in the ledger.
            Json::Value jvEscrow;
            jvEscrow[jss::TransactionType] = "EscrowCreate";
            jvEscrow[jss::Flags] = tfUniversal;
            jvEscrow[jss::Account] = gw.human();
            jvEscrow[jss::Destination] = gw.human();
            jvEscrow[jss::Amount] = XRP(100).value().getJson(0);
            jvEscrow["FinishAfter"] =
                env.now().time_since_epoch().count() + 1;
            env (jvEscrow);
            env.close();
        }
        {
            // Find the escrow.
            Json::Value const resp = acct_objs (gw, jss::escrow);

            BEAST_EXPECT (resp["result"]["account_objects"].isArray());
            BEAST_EXPECT (resp["result"]["account_objects"].size() == 1);

            auto const& escrow = resp["result"]["account_objects"][0u];
            BEAST_EXPECT (escrow["Account"] == gw.human());
            BEAST_EXPECT (escrow["Destination"] == gw.human());
            BEAST_EXPECT (escrow["Amount"].asInt() == 100'000'000);
        }
        // gw creates an offer that we can look for in the ledger.
        env (offer (gw, USD (7), XRP (14)));
        env.close();
        {
            // Find the offer.
            Json::Value const resp = acct_objs (gw, jss::offer);

            BEAST_EXPECT (resp["result"]["account_objects"].isArray());
            BEAST_EXPECT (resp["result"]["account_objects"].size() == 1);

            auto const& offer = resp["result"]["account_objects"][0u];
            BEAST_EXPECT (offer["Account"] == gw.human());
            BEAST_EXPECT (offer["TakerGets"].asInt() == 14'000'000);
            BEAST_EXPECT (offer["TakerPays"]["value"].asInt() == 7);
        }
        {
            // Create a payment channel from qw to alice that we can look for.
            Json::Value jvPayChan;
            jvPayChan[jss::TransactionType] = "PaymentChannelCreate";
            jvPayChan[jss::Flags] = tfUniversal;
            jvPayChan[jss::Account] = gw.human ();
            jvPayChan[jss::Destination] = alice.human ();
            jvPayChan[jss::Amount] = XRP (300).value().getJson (0);
            jvPayChan["SettleDelay"] = 24 * 60 * 60;
            jvPayChan["PublicKey"] = strHex (gw.pk().slice ());
            env (jvPayChan);
            env.close();
        }
        {
            // Find the payment channel.
            Json::Value const resp = acct_objs (gw, jss::payment_channel);

            BEAST_EXPECT (resp["result"]["account_objects"].isArray());
            BEAST_EXPECT (resp["result"]["account_objects"].size() == 1);

            auto const& payChan = resp["result"]["account_objects"][0u];
            BEAST_EXPECT (payChan["Account"] == gw.human());
            BEAST_EXPECT (payChan["Amount"].asInt() == 300'000'000);
            BEAST_EXPECT (payChan["SettleDelay"].asInt() == 24 * 60 * 60);
        }
        // Make gw multisigning by adding a signerList.
        env (signers (gw, 6, { { alice, 7} }));
        env.close();
        {
            // Find the signer list.
            Json::Value const resp = acct_objs (gw, jss::signer_list);

            BEAST_EXPECT (resp["result"]["account_objects"].isArray());
            BEAST_EXPECT (resp["result"]["account_objects"].size() == 1);

            auto const& signerList = resp["result"]["account_objects"][0u];
            BEAST_EXPECT (signerList["SignerQuorum"] == 6);
            auto const& entry = signerList["SignerEntries"][0u]["SignerEntry"];
            BEAST_EXPECT (entry["Account"] == alice.human());
            BEAST_EXPECT (entry["SignerWeight"].asInt() == 7);
        }
        // Create a Ticket for gw.
        env (ticket::create (gw, gw));
        env.close();
        {
            // Find the ticket.
            Json::Value const resp = acct_objs (gw, jss::ticket);

            BEAST_EXPECT (resp["result"]["account_objects"].isArray());
            BEAST_EXPECT (resp["result"]["account_objects"].size() == 1);

            auto const& ticket = resp["result"]["account_objects"][0u];
            BEAST_EXPECT (ticket["Account"] == gw.human());
            BEAST_EXPECT (ticket["LedgerEntryType"] == "Ticket");
            BEAST_EXPECT (ticket["Sequence"].asInt() == 8);
        }
        // Run up the number of directory entries so gw has two
        // directory nodes.
        for (int d = 1'000'032; d >= 1'000'000; d -= 1)
        {
            env (offer (gw, USD (1), drops (d)));
            env.close();
        }

        // Verify that the non-returning types still don't return anything.
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::account));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::amendments));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::directory));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::fee));
        BEAST_EXPECT (acct_objs_is_empty (gw, jss::hashes));
    }

    void run() override
    {
        testErrors();
        testUnsteppedThenStepped();
        testObjectTypes();
    }
};

BEAST_DEFINE_TESTSUITE(AccountObjects,app,ripple);

}
}
