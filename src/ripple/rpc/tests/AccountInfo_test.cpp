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

#include <ripple/protocol/JsonFields.h>     // jss:: definitions
#include <ripple/protocol/Feature.h>
#include <ripple/test/jtx.h>

namespace ripple {
namespace test {

class AccountInfo_test : public beast::unit_test::suite
{
public:

    void testErrors()
    {
        using namespace jtx;
        Env env(*this);
        {
            // account_info with no account.
            auto const info = env.rpc ("json", "account_info", "{ }");
            expect (info[jss::result][jss::error_message] ==
                "Missing field 'account'.");
        }
        {
            // account_info with a malformed account sting.
            auto const info = env.rpc ("json", "account_info", "{\"account\": "
                "\"n94JNrQYkDrpt62bbSR7nVEhdyAvcJXRAsjEkFYyqRkh9SUTYEqV\"}");
            expect (info[jss::result][jss::error_message] ==
                "Disallowed seed.");
        }
        {
            // account_info with an account that's not in the ledger.
            Account const bogie {"bogie"};
            auto const info = env.rpc ("json", "account_info",
                std::string ("{ ") + "\"account\": \"" + bogie.human() + "\"}");
            expect (info[jss::result][jss::error_message] ==
                "Account not found.");
        }
    }

   // Test the "signer_lists" argument in account_info.
   void testSignerLists()
    {
        using namespace jtx;
        Env env(*this, features(featureMultiSign));
        Account const alice {"alice"};
        env.fund(XRP(1000), alice);

        auto const withoutSigners = std::string ("{ ") +
            "\"account\": \"" + alice.human() + "\"}";

        auto const withSigners = std::string ("{ ") +
            "\"account\": \"" + alice.human() + "\", " +
            "\"signer_lists\": true }";

        // Alice has no SignerList yet.
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc ("json", "account_info", withoutSigners);
            expect (! info[jss::result][jss::account_data].
                isMember (jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc ("json", "account_info", withSigners);
            auto const& data = info[jss::result][jss::account_data];
            expect (data.isMember (jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            expect (signerLists.isArray());
            expect (signerLists.size() == 0);
        }

        // Give alice a SignerList.
        Account const bogie {"bogie"};

        Json::Value const smallSigners = signers(alice, 2, { { bogie, 3 } });
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc ("json", "account_info", withoutSigners);
            expect (! info[jss::result][jss::account_data].
                isMember (jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc ("json", "account_info", withSigners);
            auto const& data = info[jss::result][jss::account_data];
            expect (data.isMember (jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            expect (signerLists.isArray());
            expect (signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            expect (signers.isObject());
            expect (signers[sfSignerQuorum.jsonName] == 2);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            expect (signerEntries.size() == 1);
            auto const& entry0 = signerEntries[0u][sfSignerEntry.jsonName];
            expect (entry0[sfSignerWeight.jsonName] == 3);
        }

        // Give alice a big signer list
        Account const demon {"demon"};
        Account const ghost {"ghost"};
        Account const haunt {"haunt"};
        Account const jinni {"jinni"};
        Account const phase {"phase"};
        Account const shade {"shade"};
        Account const spook {"spook"};

        Json::Value const bigSigners = signers(alice, 4, {
            {bogie, 1}, {demon, 1}, {ghost, 1}, {haunt, 1},
            {jinni, 1}, {phase, 1}, {shade, 1}, {spook, 1}, });
        env(bigSigners);
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc ("json", "account_info", withSigners);
            auto const& data = info[jss::result][jss::account_data];
            expect (data.isMember (jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            expect (signerLists.isArray());
            expect (signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            expect (signers.isObject());
            expect (signers[sfSignerQuorum.jsonName] == 4);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            expect (signerEntries.size() == 8);
            for (unsigned i = 0u; i < 8; ++i)
            {
                auto const& entry = signerEntries[i][sfSignerEntry.jsonName];
                expect (entry.size() == 2);
                expect (entry.isMember(sfAccount.jsonName));
                expect (entry[sfSignerWeight.jsonName] == 1);
            }
        }
    }

    void run()
    {
        testErrors();
        testSignerLists();
    }
};

BEAST_DEFINE_TESTSUITE(AccountInfo,app,ripple);

}
}

