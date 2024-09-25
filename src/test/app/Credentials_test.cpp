//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <xrpld/app/tx/detail/Credentials.h>
#include <xrpld/ledger/ApplyViewImpl.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <iostream>
#include <string_view>

namespace ripple {
namespace test {

// Helper function that returns the owner count of an account root.
static inline std::uint32_t
ownerCnt(test::jtx::Env const& env, test::jtx::Account const& acct)
{
    std::uint32_t ret{0};
    if (auto const sleAcct = env.le(acct))
        ret = sleAcct->at(sfOwnerCount);
    return ret;
}

static inline bool
checkVL(
    std::shared_ptr<SLE const> const& sle,
    SField const& field,
    std::string const& expected)
{
    auto const b = sle->getFieldVL(field);
    return strHex(expected) == strHex(b);
}

static inline Keylet
credKL(
    test::jtx::Account const& subj,
    test::jtx::Account const& iss,
    std::string_view credType)
{
    return keylet::credential(
        subj.id(), iss.id(), Slice(credType.data(), credType.size()));
}

struct Credentials_test : public beast::unit_test::suite
{
    void
    testSuccessful(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";
        const char uri[] = "uri";

        {
            testcase("Credentials from issuing side.");

            using namespace jtx;
            Env env{*this, features};

            Account const iss{"issuer"};
            Account const subj{"subject"};
            Account const other{"other"};

            auto const kCred = credKL(subj, iss, credType);

            env.fund(XRP(5000), subj, iss, other);
            env.close();

            // Test Create credentials
            env(credentials::createIssuer(subj, iss, credType),
                credentials::uri(uri));
            env.close();
            {
                auto const sleCred = env.le(kCred);
                BEAST_EXPECT(static_cast<bool>(sleCred));
                if (!sleCred)
                    return;

                BEAST_EXPECT(sleCred->getAccountID(sfSubject) == subj.id());
                BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == iss.id());
                BEAST_EXPECT(!(sleCred->getFieldU32(sfFlags) & lsfAccepted));
                BEAST_EXPECT(ownerCnt(env, iss) == 1);
                BEAST_EXPECT(!ownerCnt(env, subj));
                BEAST_EXPECT(checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(checkVL(sleCred, sfURI, uri));
                auto const jle = credentials::ledgerEntryCredential(
                    env, subj, iss, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::Credential);
            }

            env(credentials::accept(subj, iss, credType));
            env.close();
            {
                // check switching owner of the credentials from isser to
                // subject
                auto const sleCred = env.le(kCred);
                BEAST_EXPECT(static_cast<bool>(sleCred));
                if (!sleCred)
                    return;

                BEAST_EXPECT(sleCred->getAccountID(sfSubject) == subj.id());
                BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == iss.id());
                BEAST_EXPECT(!ownerCnt(env, iss));
                BEAST_EXPECT(ownerCnt(env, subj) == 1);
                BEAST_EXPECT(checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(checkVL(sleCred, sfURI, uri));
                BEAST_EXPECT(sleCred->getFieldU32(sfFlags) == lsfAccepted);
            }

            env(credentials::del(subj, subj, iss, credType));
            env.close();
            {
                BEAST_EXPECT(!env.le(kCred));
                BEAST_EXPECT(!ownerCnt(env, iss));
                BEAST_EXPECT(!ownerCnt(env, subj));

                // check no credential exists anymore
                auto const jle = credentials::ledgerEntryCredential(
                    env, subj, iss, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error));
            }

            {
                testcase("Credentials for themself.");

                auto const kCred = credKL(iss, iss, credType);

                env(credentials::createIssuer(iss, iss, credType),
                    credentials::uri(uri));
                env.close();
                {
                    auto const sleCred = env.le(kCred);
                    BEAST_EXPECT(static_cast<bool>(sleCred));
                    if (!sleCred)
                        return;

                    BEAST_EXPECT(sleCred->getAccountID(sfSubject) == iss.id());
                    BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == iss.id());
                    BEAST_EXPECT((sleCred->getFieldU32(sfFlags) & lsfAccepted));
                    BEAST_EXPECT(
                        sleCred->getFieldU64(sfIssuerNode) ==
                        sleCred->getFieldU64(sfSubjectNode));
                    BEAST_EXPECT(ownerCnt(env, iss) == 1);
                    BEAST_EXPECT(checkVL(sleCred, sfCredentialType, credType));
                    BEAST_EXPECT(checkVL(sleCred, sfURI, uri));
                    auto const jle = credentials::ledgerEntryCredential(
                        env, iss, iss, credType);
                    BEAST_EXPECT(
                        jle.isObject() && jle.isMember(jss::result) &&
                        !jle[jss::result].isMember(jss::error) &&
                        jle[jss::result].isMember(jss::node) &&
                        jle[jss::result][jss::node].isMember(
                            "LedgerEntryType") &&
                        jle[jss::result][jss::node]["LedgerEntryType"] ==
                            jss::Credential);
                }

                env(credentials::del(iss, iss, iss, credType));
                env.close();
                {
                    BEAST_EXPECT(!env.le(kCred));
                    BEAST_EXPECT(!ownerCnt(env, iss));

                    // check no credential exists anymore
                    auto const jle = credentials::ledgerEntryCredential(
                        env, iss, iss, credType);
                    BEAST_EXPECT(
                        jle.isObject() && jle.isMember(jss::result) &&
                        jle[jss::result].isMember(jss::error));
                }
            }

            {
                testcase("Delete issuer");

                env(credentials::createIssuer(subj, iss, credType));
                env.close();

                // delete issuer
                {
                    int const delta = env.seq(iss) + 255;
                    for (int i = 0; i < delta; ++i)
                        env.close();
                    auto const acctDelFee{
                        drops(env.current()->fees().increment)};
                    env(acctdelete(iss, other), fee(acctDelFee));
                    env.close();
                }

                // check credentials deleted too
                {
                    BEAST_EXPECT(!env.le(kCred));
                    BEAST_EXPECT(!ownerCnt(env, subj));

                    // check no credential exists anymore
                    auto const jle = credentials::ledgerEntryCredential(
                        env, subj, iss, credType);
                    BEAST_EXPECT(
                        jle.isObject() && jle.isMember(jss::result) &&
                        jle[jss::result].isMember(jss::error));
                }

                // restore issuer
                env.fund(XRP(5000), iss);
                env.close();

                testcase("Delete subject");
                env(credentials::createIssuer(subj, iss, credType));
                env.close();
                env(credentials::accept(subj, iss, credType));
                env.close();

                // delete subject
                {
                    int const delta = env.seq(subj) + 255;
                    for (int i = 0; i < delta; ++i)
                        env.close();
                    auto const acctDelFee{
                        drops(env.current()->fees().increment)};
                    env(acctdelete(subj, other), fee(acctDelFee));
                    env.close();
                }

                // check credentials deleted too
                {
                    BEAST_EXPECT(!env.le(kCred));
                    BEAST_EXPECT(!ownerCnt(env, iss));

                    // check no credential exists anymore
                    auto const jle = credentials::ledgerEntryCredential(
                        env, subj, iss, credType);
                    BEAST_EXPECT(
                        jle.isObject() && jle.isMember(jss::result) &&
                        jle[jss::result].isMember(jss::error));
                }
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};
            Account const other{"other"};

            env.fund(XRP(5000), subj, iss, other);
            env.close();

            testcase("CredentialsDelete other");

            auto jv = credentials::createIssuer(subj, iss, credType);
            jv.removeMember(sfExpiration.jsonName);
            env(jv);
            env.close();

            // Other account delete credentials without expiration day
            jv = credentials::del(other, subj, iss, credType);
            env(jv);
            env.close();

            jv = credentials::createIssuer(subj, iss, credType);
            uint32_t const t = env.now().time_since_epoch().count();
            jv[sfExpiration.jsonName] = t;
            env(jv);
            env.close();

            // Other account delete credentials when expired
            env(credentials::del(other, subj, iss, credType));
            env.close();
        }
    }

    void
    testCreateFailed(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};

            env.fund(XRP(5000), subj, iss);
            env.close();

            {
                testcase("Credentials fail, no subject param.");
                auto jv = credentials::createIssuer(subj, iss, credType);
                jv.removeMember(jss::Subject);
                env(jv, ter(temMALFORMED));
            }

            {
                auto jv = credentials::createIssuer(subj, iss, credType);
                jv[jss::Subject] = to_string(xrpAccount());
                env(jv, ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, no credentialType param.");
                auto jv = credentials::createIssuer(subj, iss, credType);
                jv.removeMember(sfCredentialType.jsonName);
                env(jv, ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, empty credentialType param.");
                auto jv = credentials::createIssuer(subj, iss, "");
                env(jv, ter(temMALFORMED));
            }

            {
                testcase(
                    "Credentials fail, credentialType length > "
                    "maxCredentialTypeLength.");
                constexpr std::string_view longCredType =
                    "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
                    "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p   "
                    "9hfup;wDJFBVSD8f72  "
                    "pfhiusdovnbs;djvbldafghwpEFHdjfaidfgio84763tfysgdvhjasbd "
                    "vujhgWQIE7F6WEUYFGWUKEYFVQW87FGWOEFWEFUYWVEF8723GFWEFBWULE"
                    "fv28o37gfwEFB3872TFO8GSDSDVD";
                static_assert(longCredType.size() > maxCredentialTypeLength);
                auto jv = credentials::createIssuer(subj, iss, longCredType);
                env(jv, ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, URI length > 256.");
                constexpr std::string_view longURI =
                    "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
                    "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p   "
                    "9hfup;wDJFBVSD8f72  "
                    "pfhiusdovnbs;djvbldafghwpEFHdjfaidfgio84763tfysgdvhjasbd "
                    "vujhgWQIE7F6WEUYFGWUKEYFVQW87FGWOEFWEFUYWVEF8723GFWEFBWULE"
                    "fv28o37gfwEFB3872TFO8GSDSDVD";
                static_assert(longURI.size() > maxCredentialURILength);
                env(credentials::createIssuer(subj, iss, credType),
                    credentials::uri(longURI),
                    ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, URI empty.");
                env(credentials::createIssuer(subj, iss, credType),
                    credentials::uri(""),
                    ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, expiration in the past.");
                auto jv = credentials::createIssuer(subj, iss, credType);
                // current time in ripple epoch - 1s
                uint32_t const t = env.now().time_since_epoch().count() - 1;
                jv[sfExpiration.jsonName] = t;
                env(jv, ter(tecEXPIRED));
            }

            {
                testcase("Credentials fail, invalid fee.");

                auto jv = credentials::createIssuer(subj, iss, credType);
                jv[jss::Fee] = -1;
                env(jv, ter(temBAD_FEE));
            }

            {
                testcase("Credentials fail, duplicate.");
                auto const jv = credentials::createIssuer(subj, iss, credType);
                env(jv);
                env.close();
                env(jv, ter(tecDUPLICATE));
                env.close();
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};

            env.fund(XRP(5000), iss);
            env.close();

            {
                testcase("Credentials fail, subject doesn't exist.");
                auto const jv = credentials::createIssuer(subj, iss, credType);
                env(jv, ter(tecNO_TARGET));
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};

            auto const reserve = drops(env.current()->fees().accountReserve(0));
            env.fund(reserve, subj, iss);
            env.close();

            testcase("Credentials fail, not enough reserve.");
            {
                auto const jv = credentials::createIssuer(subj, iss, credType);
                env(jv, ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }
        }
    }

    void
    testAcceptFailed(FeatureBitset features)
    {
        const char credType[] = "abcde";

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};

            env.fund(XRP(5000), subj, iss);

            {
                testcase("CredentialsAccept fail, Credential doesn't exist.");
                env(credentials::accept(subj, iss, credType), ter(tecNO_ENTRY));
                env.close();
            }

            {
                testcase("CredentialsAccept fail, invalid Issuer account.");
                auto jv = credentials::accept(subj, iss, credType);
                jv[jss::Issuer] = to_string(xrpAccount());
                env(jv, ter(temINVALID_ACCOUNT_ID));
                env.close();
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};

            env.fund(drops(env.current()->fees().accountReserve(1)), iss);
            env.fund(drops(env.current()->fees().accountReserve(0)), subj);
            env.close();

            {
                testcase("CredentialsAccept fail, not enough reserve.");
                env(credentials::createIssuer(subj, iss, credType));
                env.close();

                env(credentials::accept(subj, iss, credType),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};

            env.fund(XRP(5000), subj, iss);
            env.close();

            {
                env(credentials::createIssuer(subj, iss, credType));
                env.close();

                testcase("CredentialsAccept fail, invalid fee.");
                auto jv = credentials::accept(subj, iss, credType);
                jv[jss::Fee] = -1;
                env(jv, ter(temBAD_FEE));

                testcase("CredentialsAccept fail, lsfAccepted already set.");
                env(credentials::accept(subj, iss, credType));
                env.close();
                env(credentials::accept(subj, iss, credType),
                    ter(tecDUPLICATE));
                env.close();
            }

            {
                const char credType2[] = "efghi";

                testcase("CredentialsAccept fail, expired credentials.");
                auto jv = credentials::createIssuer(subj, iss, credType2);
                uint32_t const t = env.now().time_since_epoch().count();
                jv[sfExpiration.jsonName] = t;
                env(jv);
                env.close();

                // credentials are expired now
                env(credentials::accept(subj, iss, credType2), ter(tecEXPIRED));
                env.close();

                // check that expired credentials were deleted
                auto const jDelCred = credentials::ledgerEntryCredential(
                    env, subj, iss, credType2);
                BEAST_EXPECT(
                    jDelCred.isObject() && jDelCred.isMember(jss::result) &&
                    jDelCred[jss::result].isMember(jss::error));
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};
            Account const other{"other"};

            env.fund(XRP(5000), iss, subj, other);
            env.close();

            {
                testcase("CredentialsAccept fail, issuer doesn't exist.");
                auto jv = credentials::createIssuer(subj, iss, credType);
                env(jv);
                env.close();

                // delete issuer
                int const delta = env.seq(iss) + 255;
                for (int i = 0; i < delta; ++i)
                    env.close();
                auto const acctDelFee{drops(env.current()->fees().increment)};
                env(acctdelete(iss, other), fee(acctDelFee));

                // can't accept - no issuer account
                jv = credentials::accept(subj, iss, credType);
                env(jv, ter(tecNO_ISSUER));
                env.close();
            }
        }
    }

    void
    testDeleteFailed(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};
            Account const other{"other"};

            env.fund(XRP(5000), subj, iss, other);
            env.close();

            {
                testcase("CredentialsDelete fail, no Credentials.");

                env(credentials::del(subj, subj, iss, credType),
                    ter(tecNO_ENTRY));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid Subject account.");
                auto jv = credentials::del(subj, subj, iss, credType);
                jv[jss::Subject] = to_string(xrpAccount());
                env(jv, ter(temINVALID_ACCOUNT_ID));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid Issuer account.");
                auto jv = credentials::del(subj, subj, iss, credType);
                jv[jss::Issuer] = to_string(xrpAccount());
                env(jv, ter(temINVALID_ACCOUNT_ID));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, time not expired yet.");

                auto jv = credentials::createIssuer(subj, iss, credType);
                // current time in ripple epoch + 1000s
                uint32_t const t = env.now().time_since_epoch().count() + 1000;
                jv[sfExpiration.jsonName] = t;
                env(jv);
                env.close();

                env(credentials::del(other, subj, iss, credType),
                    ter(tecNO_PERMISSION));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, no Issuer and Subject.");

                auto jv = credentials::del(subj, subj, iss, credType);
                jv.removeMember(jss::Subject);
                jv.removeMember(jss::Issuer);
                env(jv, ter(temMALFORMED));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid fee.");

                auto jv = credentials::del(subj, subj, iss, credType);
                jv[jss::Fee] = -1;
                env(jv, ter(temBAD_FEE));
                env.close();
            }

            {
                testcase("deleteSLE fail, bad SLE.");
                auto view = std::make_shared<ApplyViewImpl>(
                    env.current().get(), ApplyFlags::tapNONE);
                auto ter = CredentialDelete::deleteSLE(*view, {}, env.journal);
                BEAST_EXPECT(ter == tecNO_ENTRY);
            }
        }
    }

    void
    testFeatureFailed(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";

        {
            using namespace jtx;
            Env env{*this, features};
            Account const iss{"issuer"};
            Account const subj{"subject"};

            env.fund(XRP(5000), subj, iss);
            env.close();

            {
                testcase("Credentials fail, Feature is not enabled.");
                env(credentials::createIssuer(subj, iss, credType),
                    ter(temDISABLED));
                env(credentials::accept(subj, iss, credType), ter(temDISABLED));
                env(credentials::del(subj, subj, iss, credType),
                    ter(temDISABLED));
            }
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        testSuccessful(all);
        testCreateFailed(all);
        testAcceptFailed(all);
        testDeleteFailed(all);
        testFeatureFailed(all - featureCredentials);
    }
};

BEAST_DEFINE_TESTSUITE(Credentials, app, ripple);

}  // namespace test
}  // namespace ripple
