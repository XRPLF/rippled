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

#include <test/jtx.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class DepositAuthorized_test : public beast::unit_test::suite
{
public:
    // Helper function that builds arguments for a deposit_authorized command.
    static Json::Value
    depositAuthArgs(
        jtx::Account const& source,
        jtx::Account const& dest,
        std::string const& ledger = "",
        std::vector<std::string> const& credentials = {})
    {
        Json::Value args{Json::objectValue};
        args[jss::source_account] = source.human();
        args[jss::destination_account] = dest.human();
        if (!ledger.empty())
            args[jss::ledger_index] = ledger;

        if (!credentials.empty())
        {
            auto& arr(args[jss::credentials] = Json::arrayValue);
            for (auto const& s : credentials)
                arr.append(s);
        }

        return args;
    }

    // Helper function that verifies a deposit_authorized request was
    // successful and returned the expected value.
    void
    validateDepositAuthResult(Json::Value const& result, bool authorized)
    {
        Json::Value const& results{result[jss::result]};
        BEAST_EXPECT(results[jss::deposit_authorized] == authorized);
        BEAST_EXPECT(results[jss::status] == jss::success);
    }

    // Test a variety of non-malformed cases.
    void
    testValid()
    {
        using namespace jtx;
        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const carol{"carol"};

        Env env(*this);
        env.fund(XRP(1000), alice, becky, carol);
        env.close();

        // becky is authorized to deposit to herself.
        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(becky, becky, "validated").toStyledString()),
            true);

        // alice should currently be authorized to deposit to becky.
        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky, "validated").toStyledString()),
            true);

        // becky sets the DepositAuth flag in the current ledger.
        env(fset(becky, asfDepositAuth));

        // alice is no longer authorized to deposit to becky in current ledger.
        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky).toStyledString()),
            false);
        env.close();

        // becky is still authorized to deposit to herself.
        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(becky, becky, "validated").toStyledString()),
            true);

        // It's not a reciprocal arrangement.  becky can deposit to alice.
        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(becky, alice, "current").toStyledString()),
            true);

        // becky creates a deposit authorization for alice.
        env(deposit::auth(becky, alice));
        env.close();

        // alice is now authorized to deposit to becky.
        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky, "closed").toStyledString()),
            true);

        // carol is still not authorized to deposit to becky.
        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(carol, becky).toStyledString()),
            false);

        // becky clears the DepositAuth flag so carol becomes authorized.
        env(fclear(becky, asfDepositAuth));
        env.close();

        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(carol, becky).toStyledString()),
            true);

        // alice is still authorized to deposit to becky.
        validateDepositAuthResult(
            env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky).toStyledString()),
            true);
    }

    // Test malformed cases.
    void
    testErrors()
    {
        using namespace jtx;
        Account const alice{"alice"};
        Account const becky{"becky"};

        // Lambda that checks the (error) result of deposit_authorized.
        auto verifyErr = [this](
                             Json::Value const& result,
                             char const* error,
                             char const* errorMsg) {
            BEAST_EXPECT(result[jss::result][jss::status] == jss::error);
            BEAST_EXPECT(result[jss::result][jss::error] == error);
            BEAST_EXPECT(result[jss::result][jss::error_message] == errorMsg);
        };

        Env env(*this);
        {
            // Missing source_account field.
            Json::Value args{depositAuthArgs(alice, becky)};
            args.removeMember(jss::source_account);
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(
                result, "invalidParams", "Missing field 'source_account'.");
        }
        {
            // Non-string source_account field.
            Json::Value args{depositAuthArgs(alice, becky)};
            args[jss::source_account] = 7.3;
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(
                result,
                "invalidParams",
                "Invalid field 'source_account', not a string.");
        }
        {
            // Corrupt source_account field.
            Json::Value args{depositAuthArgs(alice, becky)};
            args[jss::source_account] = "rG1QQv2nh2gr7RCZ!P8YYcBUKCCN633jCn";
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(result, "actMalformed", "Account malformed.");
        }
        {
            // Missing destination_account field.
            Json::Value args{depositAuthArgs(alice, becky)};
            args.removeMember(jss::destination_account);
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(
                result,
                "invalidParams",
                "Missing field 'destination_account'.");
        }
        {
            // Non-string destination_account field.
            Json::Value args{depositAuthArgs(alice, becky)};
            args[jss::destination_account] = 7.3;
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(
                result,
                "invalidParams",
                "Invalid field 'destination_account', not a string.");
        }
        {
            // Corrupt destination_account field.
            Json::Value args{depositAuthArgs(alice, becky)};
            args[jss::destination_account] =
                "rP6P9ypfAmc!pw8SZHNwM4nvZHFXDraQas";
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(result, "actMalformed", "Account malformed.");
        }
        {
            // Request an invalid ledger.
            Json::Value args{depositAuthArgs(alice, becky, "-1")};
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(result, "invalidParams", "ledgerIndexMalformed");
        }
        {
            // Request a ledger that doesn't exist yet as a string.
            Json::Value args{depositAuthArgs(alice, becky, "17")};
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(result, "lgrNotFound", "ledgerNotFound");
        }
        {
            // Request a ledger that doesn't exist yet.
            Json::Value args{depositAuthArgs(alice, becky)};
            args[jss::ledger_index] = 17;
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(result, "lgrNotFound", "ledgerNotFound");
        }
        {
            // alice is not yet funded.
            Json::Value args{depositAuthArgs(alice, becky)};
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(result, "srcActNotFound", "Source account not found.");
        }
        env.fund(XRP(1000), alice);
        env.close();
        {
            // becky is not yet funded.
            Json::Value args{depositAuthArgs(alice, becky)};
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            verifyErr(
                result, "dstActNotFound", "Destination account not found.");
        }
        env.fund(XRP(1000), becky);
        env.close();
        {
            // Once becky is funded try it again and see it succeed.
            Json::Value args{depositAuthArgs(alice, becky)};
            Json::Value const result{
                env.rpc("json", "deposit_authorized", args.toStyledString())};
            validateDepositAuthResult(result, true);
        }
    }

    void
    testCredentials()
    {
        using namespace jtx;

        const char credType[] = "abcde";

        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const carol{"carol"};

        Env env(*this);
        env.fund(XRP(1000), alice, becky, carol);
        env.close();

        // carol recognize becky
        env(credentials::createIssuer(alice, carol, credType));
        env.close();
        // retrieve the index of the credentials
        auto const jCred =
            credentials::ledgerEntryCredential(env, alice, carol, credType);
        std::string const credIdx = jCred[jss::result][jss::index].asString();

        // becky sets the DepositAuth flag in the current ledger.
        env(fset(becky, asfDepositAuth));
        env.close();

        // becky authorize any account recognized by carol to make a payment
        env(deposit::authCredentials(becky, {{carol, credType}}));
        env.close();

        {
            testcase(
                "deposit_authorized with credentials failed: empty array.");

            auto args = depositAuthArgs(alice, becky, "validated");
            args[jss::credentials] = Json::arrayValue;

            auto jv =
                env.rpc("json", "deposit_authorized", args.toStyledString());
            auto const& result{jv[jss::result]};
            BEAST_EXPECT(result.isMember(jss::error));
        }

        {
            testcase(
                "deposit_authorized with credentials failed: not a string "
                "credentials");

            auto args = depositAuthArgs(alice, becky, "validated");
            args[jss::credentials] = Json::arrayValue;
            args[jss::credentials].append(1);
            args[jss::credentials].append(3);

            auto jv =
                env.rpc("json", "deposit_authorized", args.toStyledString());
            auto const& result{jv[jss::result]};
            BEAST_EXPECT(result.isMember(jss::error));
        }

        {
            testcase(
                "deposit_authorized with credentials failed: not a hex string "
                "credentials");

            auto args = depositAuthArgs(alice, becky, "validated");
            args[jss::credentials] = Json::arrayValue;
            args[jss::credentials].append("hello world");

            auto jv =
                env.rpc("json", "deposit_authorized", args.toStyledString());
            auto const& result{jv[jss::result]};
            BEAST_EXPECT(result.isMember(jss::error));
        }

        {
            testcase(
                "deposit_authorized with credentials failed: not a credential "
                "index");

            auto args = depositAuthArgs(
                alice,
                becky,
                "validated",
                {"0127AB8B4B29CCDBB61AA51C0799A8A6BB80B86A9899807C11ED576AF8516"
                 "473"});

            auto jv =
                env.rpc("json", "deposit_authorized", args.toStyledString());

            auto const& result{jv[jss::result]};
            BEAST_EXPECT(result[jss::status] == jss::error);
            BEAST_EXPECT(result[jss::error_code] == rpcBAD_CREDENTIALS);
        }

        {
            testcase(
                "deposit_authorized with credentials not authorized: "
                "credential not accepted");
            auto jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky, "validated", {credIdx})
                    .toStyledString());
            auto const& result{jv[jss::result]};
            BEAST_EXPECT(result[jss::status] == jss::error);
            BEAST_EXPECT(result[jss::error_code] == rpcBAD_CREDENTIALS);
        }

        // alice accept credentials
        env(credentials::accept(alice, carol, credType));
        env.close();

        {
            testcase("deposit_authorized with credentials");
            auto jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky, "validated", {credIdx})
                    .toStyledString());
            auto const& result{jv[jss::result]};
            BEAST_EXPECT(result[jss::status] == jss::success);
            BEAST_EXPECT(result[jss::deposit_authorized] == true);
        }

        {
            testcase("deposit_authorized  account without preauth");
            auto jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(becky, alice, "validated", {credIdx})
                    .toStyledString());
            auto const& result{jv[jss::result]};
            BEAST_EXPECT(result[jss::status] == jss::success);
            BEAST_EXPECT(result[jss::deposit_authorized] == true);
        }

        {
            testcase("deposit_authorized with expired credentials");

            // check expired credentials
            const char credType2[] = "fghijk";
            std::uint32_t const x = env.now().time_since_epoch().count() + 40;

            // create credentials with expire time 1s
            auto jv = credentials::createIssuer(alice, carol, credType2);
            jv[sfExpiration.jsonName] = x;
            env(jv);
            env.close();
            env(credentials::accept(alice, carol, credType2));
            env.close();
            auto const jCred2 = credentials::ledgerEntryCredential(
                env, alice, carol, credType2);
            std::string const credIdx2 =
                jCred2[jss::result][jss::index].asString();

            // becky sets the DepositAuth flag in the current ledger.
            env(fset(becky, asfDepositAuth));
            env.close();

            // becky authorize any account recognized by carol to make a payment
            env(deposit::authCredentials(becky, {{carol, credType2}}));
            env.close();

            {
                // this should be fine
                jv = env.rpc(
                    "json",
                    "deposit_authorized",
                    depositAuthArgs(alice, becky, "validated", {credIdx2})
                        .toStyledString());
                auto const& result{jv[jss::result]};
                BEAST_EXPECT(result[jss::status] == jss::success);
                BEAST_EXPECT(result[jss::deposit_authorized] == true);
            }

            env.close();  // increase timer by 10s
            {
                // now credentials expired
                jv = env.rpc(
                    "json",
                    "deposit_authorized",
                    depositAuthArgs(alice, becky, "validated", {credIdx2})
                        .toStyledString());

                auto const& result{jv[jss::result]};
                BEAST_EXPECT(result[jss::status] == jss::error);
                BEAST_EXPECT(result[jss::error_code] == rpcBAD_CREDENTIALS);
            }
        }
    }

    void
    run() override
    {
        testValid();
        testErrors();
        testCredentials();
    }
};

BEAST_DEFINE_TESTSUITE(DepositAuthorized, app, ripple);

}  // namespace test
}  // namespace ripple
