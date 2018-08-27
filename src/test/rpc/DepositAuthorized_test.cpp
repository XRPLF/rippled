//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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
#include <test/jtx.h>

namespace ripple {
namespace test {

class DepositAuthorized_test : public beast::unit_test::suite
{
public:
    // Helper function that builds arguments for a deposit_authorized command.
    static Json::Value depositAuthArgs (
        jtx::Account const& source,
        jtx::Account const& dest, std::string const& ledger = "")
    {
        Json::Value args {Json::objectValue};
        args[jss::source_account] = source.human();
        args[jss::destination_account] = dest.human();
        if (! ledger.empty())
            args[jss::ledger_index] = ledger;
        return args;
    }

    // Helper function that verifies a deposit_authorized request was
    // successful and returned the expected value.
    void validateDepositAuthResult (Json::Value const& result, bool authorized)
    {
        Json::Value const& results {result[jss::result]};
        BEAST_EXPECT (results[jss::deposit_authorized] == authorized);
        BEAST_EXPECT (results[jss::status] == jss::success);
    }

    // Test a variety of non-malformed cases.
    void testValid()
    {
        using namespace jtx;
        Account const alice {"alice"};
        Account const becky {"becky"};
        Account const carol {"carol"};

        Env env(*this);
        env.fund(XRP(1000), alice, becky, carol);
        env.close();

        // becky is authorized to deposit to herself.
        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (
                becky, becky, "validated").toStyledString()), true);

        // alice should currently be authorized to deposit to becky.
        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (
                alice, becky, "validated").toStyledString()), true);

        // becky sets the DepositAuth flag in the current ledger.
        env (fset (becky, asfDepositAuth));

        // alice is no longer authorized to deposit to becky in current ledger.
        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (alice, becky).toStyledString()), false);
        env.close();

        // becky is still authorized to deposit to herself.
        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (
                becky, becky, "validated").toStyledString()), true);

        // It's not a reciprocal arrangement.  becky can deposit to alice.
        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (
                becky, alice, "current").toStyledString()), true);

        // becky creates a deposit authorization for alice.
        env (deposit::auth (becky, alice));
        env.close();

        // alice is now authorized to deposit to becky.
        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (alice, becky, "closed").toStyledString()), true);

        // carol is still not authorized to deposit to becky.
        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (carol, becky).toStyledString()), false);

        // becky clears the the DepositAuth flag so carol becomes authorized.
        env (fclear (becky, asfDepositAuth));
        env.close();

        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (carol, becky).toStyledString()), true);

        // alice is still authorized to deposit to becky.
        validateDepositAuthResult (env.rpc ("json", "deposit_authorized",
            depositAuthArgs (alice, becky).toStyledString()), true);
    }

    // Test malformed cases.
    void testErrors()
    {
        using namespace jtx;
        Account const alice {"alice"};
        Account const becky {"becky"};

        // Lambda that checks the (error) result of deposit_authorized.
        auto verifyErr = [this] (
            Json::Value const& result, char const* error, char const* errorMsg)
        {
            BEAST_EXPECT (result[jss::result][jss::status] == jss::error);
            BEAST_EXPECT (result[jss::result][jss::error] == error);
            BEAST_EXPECT (result[jss::result][jss::error_message] == errorMsg);
        };

        Env env(*this);
        {
            // Missing source_account field.
            Json::Value args {depositAuthArgs (alice, becky)};
            args.removeMember (jss::source_account);
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "invalidParams",
                "Missing field 'source_account'.");
        }
        {
            // Non-string source_account field.
            Json::Value args {depositAuthArgs (alice, becky)};
            args[jss::source_account] = 7.3;
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "invalidParams",
                "Invalid field 'source_account', not a string.");
        }
        {
            // Corrupt source_account field.
            Json::Value args {depositAuthArgs (alice, becky)};
            args[jss::source_account] = "rG1QQv2nh2gr7RCZ!P8YYcBUKCCN633jCn";
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "actMalformed", "Account malformed.");
        }
        {
            // Missing destination_account field.
            Json::Value args {depositAuthArgs (alice, becky)};
            args.removeMember (jss::destination_account);
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "invalidParams",
                "Missing field 'destination_account'.");
        }
        {
            // Non-string destination_account field.
            Json::Value args {depositAuthArgs (alice, becky)};
            args[jss::destination_account] = 7.3;
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "invalidParams",
                "Invalid field 'destination_account', not a string.");
        }
        {
            // Corrupt destination_account field.
            Json::Value args {depositAuthArgs (alice, becky)};
            args[jss::destination_account] =
                "rP6P9ypfAmc!pw8SZHNwM4nvZHFXDraQas";
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "actMalformed", "Account malformed.");
        }
        {
            // Request an invalid ledger.
            Json::Value args {depositAuthArgs (alice, becky, "17")};
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "invalidParams", "ledgerIndexMalformed");
        }
        {
            // Request a ledger that doesn't exist yet.
            Json::Value args {depositAuthArgs (alice, becky)};
            args[jss::ledger_index] = 17;
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "lgrNotFound", "ledgerNotFound");
        }
        {
            // alice is not yet funded.
            Json::Value args {depositAuthArgs (alice, becky)};
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "srcActNotFound",
                "Source account not found.");
        }
        env.fund(XRP(1000), alice);
        env.close();
        {
            // becky is not yet funded.
            Json::Value args {depositAuthArgs (alice, becky)};
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            verifyErr (result, "dstActNotFound",
                "Destination account not found.");
        }
        env.fund(XRP(1000), becky);
        env.close();
        {
            // Once becky is funded try it again and see it succeed.
            Json::Value args {depositAuthArgs (alice, becky)};
            Json::Value const result {env.rpc (
                "json", "deposit_authorized", args.toStyledString())};
            validateDepositAuthResult (result, true);
        }
    }

    void run() override
    {
        testValid();
        testErrors();
    }
};

BEAST_DEFINE_TESTSUITE(DepositAuthorized,app,ripple);

}
}

