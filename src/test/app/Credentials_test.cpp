//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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
#include <xrpld/app/misc/CredentialHelpers.h>
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

static inline bool
checkVL(
    std::shared_ptr<SLE const> const& sle,
    SField const& field,
    std::string const& expected)
{
    return strHex(expected) == strHex(sle->getFieldVL(field));
}

static inline Keylet
credentialKeylet(
    test::jtx::Account const& subject,
    test::jtx::Account const& issuer,
    std::string_view credType)
{
    return keylet::credential(
        subject.id(), issuer.id(), Slice(credType.data(), credType.size()));
}

struct Credentials_test : public beast::unit_test::suite
{
    void
    testSuccessful(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";
        const char uri[] = "uri";

        Account const issuer{"issuer"};
        Account const subject{"subject"};
        Account const other{"other"};

        Env env{*this, features};

        {
            testcase("Create for subject.");

            auto const credKey = credentialKeylet(subject, issuer, credType);

            env.fund(XRP(5000), subject, issuer, other);
            env.close();

            // Test Create credentials
            env(credentials::create(subject, issuer, credType),
                credentials::uri(uri));
            env.close();
            {
                auto const sleCred = env.le(credKey);
                BEAST_EXPECT(static_cast<bool>(sleCred));
                if (!sleCred)
                    return;

                BEAST_EXPECT(sleCred->getAccountID(sfSubject) == subject.id());
                BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == issuer.id());
                BEAST_EXPECT(!sleCred->getFieldU32(sfFlags));
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(!ownerCount(env, subject));
                BEAST_EXPECT(checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(checkVL(sleCred, sfURI, uri));
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] ==
                        issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] ==
                        subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }

            env(credentials::accept(subject, issuer, credType));
            env.close();
            {
                // check switching owner of the credentials from issuer to
                // subject
                auto const sleCred = env.le(credKey);
                BEAST_EXPECT(static_cast<bool>(sleCred));
                if (!sleCred)
                    return;

                BEAST_EXPECT(sleCred->getAccountID(sfSubject) == subject.id());
                BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == issuer.id());
                BEAST_EXPECT(!ownerCount(env, issuer));
                BEAST_EXPECT(ownerCount(env, subject) == 1);
                BEAST_EXPECT(checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(checkVL(sleCred, sfURI, uri));
                BEAST_EXPECT(sleCred->getFieldU32(sfFlags) == lsfAccepted);
            }

            env(credentials::deleteCred(subject, subject, issuer, credType));
            env.close();
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, issuer));
                BEAST_EXPECT(!ownerCount(env, subject));

                // check no credential exists anymore
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }
        }

        {
            testcase("Create for themself.");

            auto const credKey = credentialKeylet(issuer, issuer, credType);

            env(credentials::create(issuer, issuer, credType),
                credentials::uri(uri));
            env.close();
            {
                auto const sleCred = env.le(credKey);
                BEAST_EXPECT(static_cast<bool>(sleCred));
                if (!sleCred)
                    return;

                BEAST_EXPECT(sleCred->getAccountID(sfSubject) == issuer.id());
                BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == issuer.id());
                BEAST_EXPECT((sleCred->getFieldU32(sfFlags) & lsfAccepted));
                BEAST_EXPECT(
                    sleCred->getFieldU64(sfIssuerNode) ==
                    sleCred->getFieldU64(sfSubjectNode));
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(checkVL(sleCred, sfURI, uri));
                auto const jle =
                    credentials::ledgerEntry(env, issuer, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] ==
                        issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] ==
                        issuer.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }

            env(credentials::deleteCred(issuer, issuer, issuer, credType));
            env.close();
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, issuer));

                // check no credential exists anymore
                auto const jle =
                    credentials::ledgerEntry(env, issuer, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }
        }
    }

    void
    testCredentialsDelete(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";

        Account const issuer{"issuer"};
        Account const subject{"subject"};
        Account const other{"other"};

        Env env{*this, features};

        // fund subject and issuer
        env.fund(XRP(5000), issuer, subject, other);
        env.close();

        {
            testcase("Delete issuer before accept");

            auto const credKey = credentialKeylet(subject, issuer, credType);
            env(credentials::create(subject, issuer, credType));
            env.close();

            // delete issuer
            {
                int const delta = env.seq(issuer) + 255;
                for (int i = 0; i < delta; ++i)
                    env.close();
                auto const acctDelFee{drops(env.current()->fees().increment)};
                env(acctdelete(issuer, other), fee(acctDelFee));
                env.close();
            }

            // check credentials deleted too
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, subject));

                // check no credential exists anymore
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }

            // resurrection
            env.fund(XRP(5000), issuer);
            env.close();
        }

        {
            testcase("Delete issuer after accept");

            auto const credKey = credentialKeylet(subject, issuer, credType);
            env(credentials::create(subject, issuer, credType));
            env.close();
            env(credentials::accept(subject, issuer, credType));
            env.close();

            // delete issuer
            {
                int const delta = env.seq(issuer) + 255;
                for (int i = 0; i < delta; ++i)
                    env.close();
                auto const acctDelFee{drops(env.current()->fees().increment)};
                env(acctdelete(issuer, other), fee(acctDelFee));
                env.close();
            }

            // check credentials deleted too
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, subject));

                // check no credential exists anymore
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }

            // resurrection
            env.fund(XRP(5000), issuer);
            env.close();
        }

        {
            testcase("Delete subject before accept");

            auto const credKey = credentialKeylet(subject, issuer, credType);
            env(credentials::create(subject, issuer, credType));
            env.close();

            // delete subject
            {
                int const delta = env.seq(subject) + 255;
                for (int i = 0; i < delta; ++i)
                    env.close();
                auto const acctDelFee{drops(env.current()->fees().increment)};
                env(acctdelete(subject, other), fee(acctDelFee));
                env.close();
            }

            // check credentials deleted too
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, issuer));

                // check no credential exists anymore
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }

            // resurrection
            env.fund(XRP(5000), subject);
            env.close();
        }

        {
            testcase("Delete subject after accept");

            auto const credKey = credentialKeylet(subject, issuer, credType);
            env(credentials::create(subject, issuer, credType));
            env.close();
            env(credentials::accept(subject, issuer, credType));
            env.close();

            // delete subject
            {
                int const delta = env.seq(subject) + 255;
                for (int i = 0; i < delta; ++i)
                    env.close();
                auto const acctDelFee{drops(env.current()->fees().increment)};
                env(acctdelete(subject, other), fee(acctDelFee));
                env.close();
            }

            // check credentials deleted too
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, issuer));

                // check no credential exists anymore
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }

            // resurrection
            env.fund(XRP(5000), subject);
            env.close();
        }

        {
            testcase("Delete by other");

            auto const credKey = credentialKeylet(subject, issuer, credType);
            auto jv = credentials::create(subject, issuer, credType);
            uint32_t const t = env.current()
                                   ->info()
                                   .parentCloseTime.time_since_epoch()
                                   .count();
            jv[sfExpiration.jsonName] = t + 20;
            env(jv);

            // time advance
            env.close();
            env.close();
            env.close();

            // Other account delete credentials
            env(credentials::deleteCred(other, subject, issuer, credType));
            env.close();

            // check credentials object
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, issuer));
                BEAST_EXPECT(!ownerCount(env, subject));

                // check no credential exists anymore
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }
        }

        {
            testcase("Delete by subject");

            env(credentials::create(subject, issuer, credType));
            env.close();

            // Subject can delete
            env(credentials::deleteCred(subject, subject, issuer, credType));
            env.close();
            {
                auto const credKey =
                    credentialKeylet(subject, issuer, credType);
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, subject));
                BEAST_EXPECT(!ownerCount(env, issuer));
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }
        }

        {
            testcase("Delete by issuer");
            env(credentials::create(subject, issuer, credType));
            env.close();

            env(credentials::deleteCred(issuer, subject, issuer, credType));
            env.close();
            {
                auto const credKey =
                    credentialKeylet(subject, issuer, credType);
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, subject));
                BEAST_EXPECT(!ownerCount(env, issuer));
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }
        }
    }

    void
    testCreateFailed(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";

        Account const issuer{"issuer"};
        Account const subject{"subject"};

        {
            using namespace jtx;
            Env env{*this, features};

            env.fund(XRP(5000), subject, issuer);
            env.close();

            {
                testcase("Credentials fail, no subject param.");
                auto jv = credentials::create(subject, issuer, credType);
                jv.removeMember(jss::Subject);
                env(jv, ter(temMALFORMED));
            }

            {
                auto jv = credentials::create(subject, issuer, credType);
                jv[jss::Subject] = to_string(xrpAccount());
                env(jv, ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, no credentialType param.");
                auto jv = credentials::create(subject, issuer, credType);
                jv.removeMember(sfCredentialType.jsonName);
                env(jv, ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, empty credentialType param.");
                auto jv = credentials::create(subject, issuer, "");
                env(jv, ter(temMALFORMED));
            }

            {
                testcase(
                    "Credentials fail, credentialType length > "
                    "maxCredentialTypeLength.");
                constexpr std::string_view longCredType =
                    "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
                    "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p";
                static_assert(longCredType.size() > maxCredentialTypeLength);
                auto jv = credentials::create(subject, issuer, longCredType);
                env(jv, ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, URI length > 256.");
                constexpr std::string_view longURI =
                    "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
                    "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p   "
                    "9hfup;wDJFBVSD8f72  "
                    "pfhiusdovnbs;"
                    "djvbldafghwpEFHdjfaidfgio84763tfysgdvhjasbd "
                    "vujhgWQIE7F6WEUYFGWUKEYFVQW87FGWOEFWEFUYWVEF8723GFWEFB"
                    "WULE"
                    "fv28o37gfwEFB3872TFO8GSDSDVD";
                static_assert(longURI.size() > maxCredentialURILength);
                env(credentials::create(subject, issuer, credType),
                    credentials::uri(longURI),
                    ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, URI empty.");
                env(credentials::create(subject, issuer, credType),
                    credentials::uri(""),
                    ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, expiration in the past.");
                auto jv = credentials::create(subject, issuer, credType);
                // current time in ripple epoch - 1s
                uint32_t const t = env.current()
                                       ->info()
                                       .parentCloseTime.time_since_epoch()
                                       .count() -
                    1;
                jv[sfExpiration.jsonName] = t;
                env(jv, ter(tecEXPIRED));
            }

            {
                testcase("Credentials fail, invalid fee.");

                auto jv = credentials::create(subject, issuer, credType);
                jv[jss::Fee] = -1;
                env(jv, ter(temBAD_FEE));
            }

            {
                testcase("Credentials fail, duplicate.");
                auto const jv = credentials::create(subject, issuer, credType);
                env(jv);
                env.close();
                env(jv, ter(tecDUPLICATE));
                env.close();

                // check credential still present
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] ==
                        issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] ==
                        subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};

            env.fund(XRP(5000), issuer);
            env.close();

            {
                testcase("Credentials fail, subject doesn't exist.");
                auto const jv = credentials::create(subject, issuer, credType);
                env(jv, ter(tecNO_TARGET));
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};

            auto const reserve = drops(env.current()->fees().accountReserve(0));
            env.fund(reserve, subject, issuer);
            env.close();

            testcase("Credentials fail, not enough reserve.");
            {
                auto const jv = credentials::create(subject, issuer, credType);
                env(jv, ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }
        }
    }

    void
    testAcceptFailed(FeatureBitset features)
    {
        using namespace jtx;

        const char credType[] = "abcde";
        Account const issuer{"issuer"};
        Account const subject{"subject"};
        Account const other{"other"};

        {
            Env env{*this, features};

            env.fund(XRP(5000), subject, issuer);

            {
                testcase("CredentialsAccept fail, Credential doesn't exist.");
                env(credentials::accept(subject, issuer, credType),
                    ter(tecNO_ENTRY));
                env.close();
            }

            {
                testcase("CredentialsAccept fail, invalid Issuer account.");
                auto jv = credentials::accept(subject, issuer, credType);
                jv[jss::Issuer] = to_string(xrpAccount());
                env(jv, ter(temINVALID_ACCOUNT_ID));
                env.close();
            }

            {
                testcase(
                    "CredentialsAccept fail, invalid credentialType param.");
                auto jv = credentials::accept(subject, issuer, "");
                env(jv, ter(temMALFORMED));
            }
        }

        {
            Env env{*this, features};

            env.fund(drops(env.current()->fees().accountReserve(1)), issuer);
            env.fund(drops(env.current()->fees().accountReserve(0)), subject);
            env.close();

            {
                testcase("CredentialsAccept fail, not enough reserve.");
                env(credentials::create(subject, issuer, credType));
                env.close();

                env(credentials::accept(subject, issuer, credType),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                // check credential still present
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] ==
                        issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] ==
                        subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};

            env.fund(XRP(5000), subject, issuer);
            env.close();

            {
                env(credentials::create(subject, issuer, credType));
                env.close();

                testcase("CredentialsAccept fail, invalid fee.");
                auto jv = credentials::accept(subject, issuer, credType);
                jv[jss::Fee] = -1;
                env(jv, ter(temBAD_FEE));

                testcase("CredentialsAccept fail, lsfAccepted already set.");
                env(credentials::accept(subject, issuer, credType));
                env.close();
                env(credentials::accept(subject, issuer, credType),
                    ter(tecDUPLICATE));
                env.close();

                // check credential still present
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] ==
                        issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] ==
                        subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }

            {
                const char credType2[] = "efghi";

                testcase("CredentialsAccept fail, expired credentials.");
                auto jv = credentials::create(subject, issuer, credType2);
                uint32_t const t = env.current()
                                       ->info()
                                       .parentCloseTime.time_since_epoch()
                                       .count();
                jv[sfExpiration.jsonName] = t;
                env(jv);
                env.close();

                // credentials are expired now
                env(credentials::accept(subject, issuer, credType2),
                    ter(tecEXPIRED));
                env.close();

                // check that expired credentials were deleted
                auto const jDelCred =
                    credentials::ledgerEntry(env, subject, issuer, credType2);
                BEAST_EXPECT(
                    jDelCred.isObject() && jDelCred.isMember(jss::result) &&
                    jDelCred[jss::result].isMember(jss::error) &&
                    jDelCred[jss::result][jss::error] == "entryNotFound");

                BEAST_EXPECT(ownerCount(env, issuer) == 0);
                BEAST_EXPECT(ownerCount(env, subject) == 1);
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};

            env.fund(XRP(5000), issuer, subject, other);
            env.close();

            {
                testcase("CredentialsAccept fail, issuer doesn't exist.");
                auto jv = credentials::create(subject, issuer, credType);
                env(jv);
                env.close();

                // delete issuer
                int const delta = env.seq(issuer) + 255;
                for (int i = 0; i < delta; ++i)
                    env.close();
                auto const acctDelFee{drops(env.current()->fees().increment)};
                env(acctdelete(issuer, other), fee(acctDelFee));

                // can't accept - no issuer account
                jv = credentials::accept(subject, issuer, credType);
                env(jv, ter(tecNO_ISSUER));
                env.close();

                // check that expired credentials were deleted
                auto const jDelCred =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jDelCred.isObject() && jDelCred.isMember(jss::result) &&
                    jDelCred[jss::result].isMember(jss::error) &&
                    jDelCred[jss::result][jss::error] == "entryNotFound");
            }
        }
    }

    void
    testDeleteFailed(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";
        Account const issuer{"issuer"};
        Account const subject{"subject"};
        Account const other{"other"};

        {
            using namespace jtx;
            Env env{*this, features};

            env.fund(XRP(5000), subject, issuer, other);
            env.close();

            {
                testcase("CredentialsDelete fail, no Credentials.");
                env(credentials::deleteCred(subject, subject, issuer, credType),
                    ter(tecNO_ENTRY));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid Subject account.");
                auto jv =
                    credentials::deleteCred(subject, subject, issuer, credType);
                jv[jss::Subject] = to_string(xrpAccount());
                env(jv, ter(temINVALID_ACCOUNT_ID));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid Issuer account.");
                auto jv =
                    credentials::deleteCred(subject, subject, issuer, credType);
                jv[jss::Issuer] = to_string(xrpAccount());
                env(jv, ter(temINVALID_ACCOUNT_ID));
                env.close();
            }

            {
                testcase(
                    "CredentialsDelete fail, invalid credentialType param.");
                auto jv = credentials::deleteCred(subject, subject, issuer, "");
                env(jv, ter(temMALFORMED));
            }

            {
                const char credType2[] = "fghij";

                env(credentials::create(subject, issuer, credType2));
                env.close();

                // Other account can't delete credentials without expiration
                env(credentials::deleteCred(other, subject, issuer, credType2),
                    ter(tecNO_PERMISSION));
                env.close();

                // check credential still present
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType2);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] ==
                        issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] ==
                        subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType2)));
            }

            {
                testcase("CredentialsDelete fail, time not expired yet.");

                auto jv = credentials::create(subject, issuer, credType);
                // current time in ripple epoch + 1000s
                uint32_t const t = env.current()
                                       ->info()
                                       .parentCloseTime.time_since_epoch()
                                       .count() +
                    1000;
                jv[sfExpiration.jsonName] = t;
                env(jv);
                env.close();

                // Other account can't delete credentials that not expired
                env(credentials::deleteCred(other, subject, issuer, credType),
                    ter(tecNO_PERMISSION));
                env.close();

                // check credential still present
                auto const jle =
                    credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] ==
                        issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] ==
                        subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }

            {
                testcase("CredentialsDelete fail, no Issuer and Subject.");

                auto jv =
                    credentials::deleteCred(subject, subject, issuer, credType);
                jv.removeMember(jss::Subject);
                jv.removeMember(jss::Issuer);
                env(jv, ter(temMALFORMED));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid fee.");

                auto jv =
                    credentials::deleteCred(subject, subject, issuer, credType);
                jv[jss::Fee] = -1;
                env(jv, ter(temBAD_FEE));
                env.close();
            }

            {
                testcase("deleteSLE fail, bad SLE.");
                auto view = std::make_shared<ApplyViewImpl>(
                    env.current().get(), ApplyFlags::tapNONE);
                auto ter =
                    ripple::credentials::deleteSLE(*view, {}, env.journal);
                BEAST_EXPECT(ter == tecNO_ENTRY);
            }
        }
    }

    void
    testFeatureFailed(FeatureBitset features)
    {
        using namespace test::jtx;

        const char credType[] = "abcde";
        Account const issuer{"issuer"};
        Account const subject{"subject"};

        {
            using namespace jtx;
            Env env{*this, features};

            env.fund(XRP(5000), subject, issuer);
            env.close();

            {
                testcase("Credentials fail, Feature is not enabled.");
                env(credentials::create(subject, issuer, credType),
                    ter(temDISABLED));
                env(credentials::accept(subject, issuer, credType),
                    ter(temDISABLED));
                env(credentials::deleteCred(subject, subject, issuer, credType),
                    ter(temDISABLED));
            }
        }
    }

    void
    testRPC()
    {
        using namespace test::jtx;

        const char credType[] = "abcde";
        Account const issuer{"issuer"};
        Account const subject{"subject"};

        {
            using namespace jtx;
            Env env{*this};

            env.fund(XRP(5000), subject, issuer);
            env.close();

            env(credentials::create(subject, issuer, credType));
            env.close();

            env(credentials::accept(subject, issuer, credType));
            env.close();

            testcase("account_tx");

            std::string txHash0, txHash1;
            {
                Json::Value params;
                params[jss::account] = subject.human();
                auto const jv = env.rpc(
                    "json", "account_tx", to_string(params))[jss::result];

                BEAST_EXPECT(jv[jss::transactions].size() == 4);
                auto const& tx0(jv[jss::transactions][0u][jss::tx]);
                BEAST_EXPECT(
                    tx0[jss::TransactionType] == jss::CredentialAccept);
                auto const& tx1(jv[jss::transactions][1u][jss::tx]);
                BEAST_EXPECT(
                    tx1[jss::TransactionType] == jss::CredentialCreate);
                txHash0 = tx0[jss::hash].asString();
                txHash1 = tx1[jss::hash].asString();
            }

            {
                Json::Value params;
                params[jss::account] = issuer.human();
                auto const jv = env.rpc(
                    "json", "account_tx", to_string(params))[jss::result];

                BEAST_EXPECT(jv[jss::transactions].size() == 4);
                auto const& tx0(jv[jss::transactions][0u][jss::tx]);
                BEAST_EXPECT(
                    tx0[jss::TransactionType] == jss::CredentialAccept);
                auto const& tx1(jv[jss::transactions][1u][jss::tx]);
                BEAST_EXPECT(
                    tx1[jss::TransactionType] == jss::CredentialCreate);

                BEAST_EXPECT(txHash0 == tx0[jss::hash].asString());
                BEAST_EXPECT(txHash1 == tx1[jss::hash].asString());
            }

            testcase("account_objects");
            std::string objectIdx;
            {
                Json::Value params;
                params[jss::account] = subject.human();
                auto jv = env.rpc(
                    "json", "account_objects", to_string(params))[jss::result];

                BEAST_EXPECT(jv[jss::account_objects].size() == 1);
                auto const& object(jv[jss::account_objects][0u]);

                BEAST_EXPECT(
                    object["LedgerEntryType"].asString() == jss::Credential);
                objectIdx = object[jss::index].asString();
            }

            {
                Json::Value params;
                params[jss::account] = issuer.human();
                auto jv = env.rpc(
                    "json", "account_objects", to_string(params))[jss::result];

                BEAST_EXPECT(jv[jss::account_objects].size() == 1);
                auto const& object(jv[jss::account_objects][0u]);

                BEAST_EXPECT(
                    object["LedgerEntryType"].asString() == jss::Credential);
                BEAST_EXPECT(objectIdx == object[jss::index].asString());
            }
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        testSuccessful(all);
        testCredentialsDelete(all);
        testCreateFailed(all);
        testAcceptFailed(all);
        testDeleteFailed(all);
        testFeatureFailed(all - featureCredentials);
        testRPC();
    }
};

BEAST_DEFINE_TESTSUITE(Credentials, app, ripple);

}  // namespace test
}  // namespace ripple
