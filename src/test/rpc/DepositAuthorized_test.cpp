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
    checkCredentialsResponse(
        Json::Value const& result,
        jtx::Account const& src,
        jtx::Account const& dst,
        bool authorized,
        std::vector<std::string> credentialIDs = {},
        std::string_view error = "")
    {
        BEAST_EXPECT(
            result[jss::status] == authorized ? jss::success : jss::error);
        if (result.isMember(jss::deposit_authorized))
            BEAST_EXPECT(result[jss::deposit_authorized] == authorized);
        if (authorized)
            BEAST_EXPECT(
                result.isMember(jss::deposit_authorized) &&
                (result[jss::deposit_authorized] == true));

        BEAST_EXPECT(result.isMember(jss::error) == !error.empty());
        if (!error.empty())
            BEAST_EXPECT(result[jss::error].asString() == error);

        if (authorized)
        {
            BEAST_EXPECT(result[jss::source_account] == src.human());
            BEAST_EXPECT(result[jss::destination_account] == dst.human());

            for (unsigned i = 0; i < credentialIDs.size(); ++i)
                BEAST_EXPECT(result[jss::credentials][i] == credentialIDs[i]);
        }
        else
        {
            BEAST_EXPECT(result[jss::request].isObject());

            auto const& request = result[jss::request];
            BEAST_EXPECT(request[jss::command] == jss::deposit_authorized);
            BEAST_EXPECT(request[jss::source_account] == src.human());
            BEAST_EXPECT(request[jss::destination_account] == dst.human());

            for (unsigned i = 0; i < credentialIDs.size(); ++i)
                BEAST_EXPECT(request[jss::credentials][i] == credentialIDs[i]);
        }
    }

    void
    testCredentials()
    {
        using namespace jtx;

        const char credType[] = "abcde";

        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const diana{"diana"};
        Account const carol{"carol"};

        Env env(*this);
        env.fund(XRP(1000), alice, becky, carol, diana);
        env.close();

        // carol recognize alice
        env(credentials::create(alice, carol, credType));
        env.close();
        // retrieve the index of the credentials
        auto const jv = credentials::ledgerEntry(env, alice, carol, credType);
        std::string const credIdx = jv[jss::result][jss::index].asString();

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

            auto const jv =
                env.rpc("json", "deposit_authorized", args.toStyledString());
            checkCredentialsResponse(
                jv[jss::result], alice, becky, false, {}, "invalidParams");
        }

        {
            testcase(
                "deposit_authorized with credentials failed: not a string "
                "credentials");

            auto args = depositAuthArgs(alice, becky, "validated");
            args[jss::credentials] = Json::arrayValue;
            args[jss::credentials].append(1);
            args[jss::credentials].append(3);

            auto const jv =
                env.rpc("json", "deposit_authorized", args.toStyledString());
            checkCredentialsResponse(
                jv[jss::result], alice, becky, false, {}, "invalidParams");
        }

        {
            testcase(
                "deposit_authorized with credentials failed: not a hex string "
                "credentials");

            auto args = depositAuthArgs(alice, becky, "validated");
            args[jss::credentials] = Json::arrayValue;
            args[jss::credentials].append("hello world");

            auto const jv =
                env.rpc("json", "deposit_authorized", args.toStyledString());
            checkCredentialsResponse(
                jv[jss::result],
                alice,
                becky,
                false,
                {"hello world"},
                "invalidParams");
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

            auto const jv =
                env.rpc("json", "deposit_authorized", args.toStyledString());
            checkCredentialsResponse(
                jv[jss::result],
                alice,
                becky,
                false,
                {"0127AB8B4B29CCDBB61AA51C0799A8A6BB80B86A9899807C11ED576AF8516"
                 "473"},
                "badCredentials");
        }

        {
            testcase(
                "deposit_authorized with credentials not authorized: "
                "credential not accepted");
            auto const jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky, "validated", {credIdx})
                    .toStyledString());
            checkCredentialsResponse(
                jv[jss::result],
                alice,
                becky,
                false,
                {credIdx},
                "badCredentials");
        }

        // alice accept credentials
        env(credentials::accept(alice, carol, credType));
        env.close();

        {
            testcase("deposit_authorized with duplicates in credentials");
            auto const jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky, "validated", {credIdx, credIdx})
                    .toStyledString());
            checkCredentialsResponse(
                jv[jss::result],
                alice,
                becky,
                false,
                {credIdx, credIdx},
                "badCredentials");
        }

        {
            static const std::vector<std::string> credIds = {
                "18004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4",
                "28004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4",
                "38004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4",
                "48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4",
                "58004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4",
                "68004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4",
                "78004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4",
                "88004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4",
                "98004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4"};
            assert(credIds.size() > maxCredentialsArraySize);

            testcase("deposit_authorized too long credentials");
            auto const jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky, "validated", credIds)
                    .toStyledString());
            checkCredentialsResponse(
                jv[jss::result], alice, becky, false, credIds, "invalidParams");
        }

        {
            testcase("deposit_authorized with credentials");
            auto const jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(alice, becky, "validated", {credIdx})
                    .toStyledString());
            checkCredentialsResponse(
                jv[jss::result], alice, becky, true, {credIdx});
        }

        {
            // diana recognize becky
            env(credentials::create(becky, diana, credType));
            env.close();
            env(credentials::accept(becky, diana, credType));
            env.close();

            // retrieve the index of the credentials
            auto jv = credentials::ledgerEntry(env, becky, diana, credType);
            std::string const credBecky =
                jv[jss::result][jss::index].asString();

            testcase("deposit_authorized account without preauth");
            jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(becky, alice, "validated", {credBecky})
                    .toStyledString());
            checkCredentialsResponse(
                jv[jss::result], becky, alice, true, {credBecky});
        }

        {
            // carol recognize diana
            env(credentials::create(diana, carol, credType));
            env.close();
            env(credentials::accept(diana, carol, credType));
            env.close();
            // retrieve the index of the credentials
            auto jv = credentials::ledgerEntry(env, alice, carol, credType);
            std::string const credDiana =
                jv[jss::result][jss::index].asString();

            // alice try to use credential for different account
            jv = env.rpc(
                "json",
                "deposit_authorized",
                depositAuthArgs(becky, alice, "validated", {credDiana})
                    .toStyledString());
            checkCredentialsResponse(
                jv[jss::result],
                becky,
                alice,
                false,
                {credDiana},
                "badCredentials");
        }

        {
            testcase("deposit_authorized with expired credentials");

            // check expired credentials
            const char credType2[] = "fghijk";
            std::uint32_t const x = env.current()
                                        ->info()
                                        .parentCloseTime.time_since_epoch()
                                        .count() +
                40;

            // create credentials with expire time 40s
            auto jv = credentials::create(alice, carol, credType2);
            jv[sfExpiration.jsonName] = x;
            env(jv);
            env.close();
            env(credentials::accept(alice, carol, credType2));
            env.close();
            jv = credentials::ledgerEntry(env, alice, carol, credType2);
            std::string const credIdx2 = jv[jss::result][jss::index].asString();

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
                checkCredentialsResponse(
                    jv[jss::result], alice, becky, true, {credIdx2});
            }

            // increase timer by 20s
            env.close();
            env.close();
            {
                // now credentials expired
                jv = env.rpc(
                    "json",
                    "deposit_authorized",
                    depositAuthArgs(alice, becky, "validated", {credIdx2})
                        .toStyledString());

                checkCredentialsResponse(
                    jv[jss::result],
                    alice,
                    becky,
                    false,
                    {credIdx2},
                    "badCredentials");
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
