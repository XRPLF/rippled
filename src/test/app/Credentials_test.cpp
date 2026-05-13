
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/deposit.h>
#include <test/jtx/directory.h>
#include <test/jtx/fee.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

namespace xrpl::test {

struct Credentials_test : public beast::unit_test::Suite
{
    void
    testSuccessful(FeatureBitset features)
    {
        using namespace test::jtx;

        char const credType[] = "abcde";
        char const uri[] = "uri";

        Account const issuer{"issuer"};
        Account const subject{"subject"};
        Account const other{"other"};

        Env env{*this, features};

        {
            testcase("Create for subject.");

            auto const credKey = credentials::keylet(subject, issuer, credType);

            env.fund(XRP(5000), subject, issuer, other);
            env.close();

            // Test Create credentials
            env(credentials::create(subject, issuer, credType), credentials::Uri(uri));
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
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] == subject.human() &&
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
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    jle[jss::result].isMember(jss::error) &&
                    jle[jss::result][jss::error] == "entryNotFound");
            }
        }

        {
            testcase("Create for themself.");

            auto const credKey = credentials::keylet(issuer, issuer, credType);

            env(credentials::create(issuer, issuer, credType), credentials::Uri(uri));
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
                    sleCred->getFieldU64(sfIssuerNode) == sleCred->getFieldU64(sfSubjectNode));
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(checkVL(sleCred, sfURI, uri));
                auto const jle = credentials::ledgerEntry(env, issuer, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] == issuer.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }

            env(credentials::deleteCred(issuer, issuer, issuer, credType));
            env.close();
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, issuer));

                // check no credential exists anymore
                auto const jle = credentials::ledgerEntry(env, issuer, issuer, credType);
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

        char const credType[] = "abcde";

        Account const issuer{"issuer"};
        Account const subject{"subject"};
        Account const other{"other"};

        Env env{*this, features};

        // fund subject and issuer
        env.fund(XRP(5000), issuer, subject, other);
        env.close();

        {
            testcase("Delete issuer before accept");

            auto const credKey = credentials::keylet(subject, issuer, credType);
            env(credentials::create(subject, issuer, credType));
            env.close();

            // delete issuer
            {
                int const delta = env.seq(issuer) + 255;
                for (int i = 0; i < delta; ++i)
                    env.close();
                auto const acctDelFee{drops(env.current()->fees().increment)};
                env(acctdelete(issuer, other), Fee(acctDelFee));
                env.close();
            }

            // check credentials deleted too
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, subject));

                // check no credential exists anymore
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
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

            auto const credKey = credentials::keylet(subject, issuer, credType);
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
                env(acctdelete(issuer, other), Fee(acctDelFee));
                env.close();
            }

            // check credentials deleted too
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, subject));

                // check no credential exists anymore
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
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

            auto const credKey = credentials::keylet(subject, issuer, credType);
            env(credentials::create(subject, issuer, credType));
            env.close();

            // delete subject
            {
                int const delta = env.seq(subject) + 255;
                for (int i = 0; i < delta; ++i)
                    env.close();
                auto const acctDelFee{drops(env.current()->fees().increment)};
                env(acctdelete(subject, other), Fee(acctDelFee));
                env.close();
            }

            // check credentials deleted too
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, issuer));

                // check no credential exists anymore
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
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

            auto const credKey = credentials::keylet(subject, issuer, credType);
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
                env(acctdelete(subject, other), Fee(acctDelFee));
                env.close();
            }

            // check credentials deleted too
            {
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, issuer));

                // check no credential exists anymore
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
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

            auto const credKey = credentials::keylet(subject, issuer, credType);
            auto jv = credentials::create(subject, issuer, credType);
            uint32_t const t = env.current()->header().parentCloseTime.time_since_epoch().count();
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
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
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
                auto const credKey = credentials::keylet(subject, issuer, credType);
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, subject));
                BEAST_EXPECT(!ownerCount(env, issuer));
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
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
                auto const credKey = credentials::keylet(subject, issuer, credType);
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(!ownerCount(env, subject));
                BEAST_EXPECT(!ownerCount(env, issuer));
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
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

        char const credType[] = "abcde";

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
                env(jv, Ter(temMALFORMED));
            }

            {
                auto jv = credentials::create(subject, issuer, credType);
                jv[jss::Subject] = to_string(xrpAccount());
                env(jv, Ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, no credentialType param.");
                auto jv = credentials::create(subject, issuer, credType);
                jv.removeMember(sfCredentialType.jsonName);
                env(jv, Ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, empty credentialType param.");
                auto jv = credentials::create(subject, issuer, "");
                env(jv, Ter(temMALFORMED));
            }

            {
                testcase(
                    "Credentials fail, credentialType length > "
                    "maxCredentialTypeLength.");
                constexpr std::string_view kLONG_CRED_TYPE =
                    "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
                    "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p";
                static_assert(kLONG_CRED_TYPE.size() > kMAX_CREDENTIAL_TYPE_LENGTH);
                auto jv = credentials::create(subject, issuer, kLONG_CRED_TYPE);
                env(jv, Ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, URI length > 256.");
                constexpr std::string_view kLONG_URI =
                    "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
                    "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p   "
                    "9hfup;wDJFBVSD8f72  "
                    "pfhiusdovnbs;"
                    "djvbldafghwpEFHdjfaidfgio84763tfysgdvhjasbd "
                    "vujhgWQIE7F6WEUYFGWUKEYFVQW87FGWOEFWEFUYWVEF8723GFWEFB"
                    "WULE"
                    "fv28o37gfwEFB3872TFO8GSDSDVD";
                static_assert(kLONG_URI.size() > kMAX_CREDENTIAL_URI_LENGTH);
                env(credentials::create(subject, issuer, credType),
                    credentials::Uri(kLONG_URI),
                    Ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, URI empty.");
                env(credentials::create(subject, issuer, credType),
                    credentials::Uri(""),
                    Ter(temMALFORMED));
            }

            {
                testcase("Credentials fail, expiration in the past.");
                auto jv = credentials::create(subject, issuer, credType);
                // current time in XRPL epoch - 1s
                uint32_t const t =
                    env.current()->header().parentCloseTime.time_since_epoch().count() - 1;
                jv[sfExpiration.jsonName] = t;
                env(jv, Ter(tecEXPIRED));
            }

            {
                testcase("Credentials fail, invalid fee.");

                auto jv = credentials::create(subject, issuer, credType);
                jv[jss::Fee] = -1;
                env(jv, Ter(temBAD_FEE));
            }

            {
                testcase("Credentials fail, duplicate.");
                auto const jv = credentials::create(subject, issuer, credType);
                env(jv);
                env.close();
                env(jv, Ter(tecDUPLICATE));
                env.close();

                // check credential still present
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] == subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }

            {
                testcase("Credentials fail, directory full");
                std::uint32_t const issuerSeq{env.seq(issuer) + 1};
                env(ticket::create(issuer, 63));
                env.close();

                // Everything below can only be tested on open ledger.
                auto const res1 = directory::bumpLastPage(
                    env,
                    directory::maximumPageIndex(env),
                    keylet::ownerDir(issuer.id()),
                    directory::adjustOwnerNode);
                BEAST_EXPECT(res1);

                // NOLINTNEXTLINE(readability-suspicious-call-argument)
                auto const jv = credentials::create(issuer, subject, credType);
                env(jv, Ter(tecDIR_FULL));
                // Free one directory entry by using a ticket
                env(noop(issuer), ticket::Use(issuerSeq + 40));

                // Fill subject directory
                env(ticket::create(subject, 63));
                auto const res2 = directory::bumpLastPage(
                    env,
                    directory::maximumPageIndex(env),
                    keylet::ownerDir(subject.id()),
                    directory::adjustOwnerNode);
                BEAST_EXPECT(res2);
                env(jv, Ter(tecDIR_FULL));

                // End test
                env.close();
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
                env(jv, Ter(tecNO_TARGET));
            }
        }

        {
            using namespace jtx;
            Env env{*this, features};

            auto const reserve = drops(env.current()->fees().reserve);
            env.fund(reserve, subject, issuer);
            env.close();

            testcase("Credentials fail, not enough reserve.");
            {
                auto const jv = credentials::create(subject, issuer, credType);
                env(jv, Ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }
        }
    }

    void
    testAcceptFailed(FeatureBitset features)
    {
        using namespace jtx;

        char const credType[] = "abcde";
        Account const issuer{"issuer"};
        Account const subject{"subject"};
        Account const other{"other"};

        {
            Env env{*this, features};

            env.fund(XRP(5000), subject, issuer);

            {
                testcase("CredentialsAccept fail, Credential doesn't exist.");
                env(credentials::accept(subject, issuer, credType), Ter(tecNO_ENTRY));
                env.close();
            }

            {
                testcase("CredentialsAccept fail, invalid Issuer account.");
                auto jv = credentials::accept(subject, issuer, credType);
                jv[jss::Issuer] = to_string(xrpAccount());
                env(jv, Ter(temINVALID_ACCOUNT_ID));
                env.close();
            }

            {
                testcase("CredentialsAccept fail, invalid credentialType param.");
                auto jv = credentials::accept(subject, issuer, "");
                env(jv, Ter(temMALFORMED));
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

                env(credentials::accept(subject, issuer, credType), Ter(tecINSUFFICIENT_RESERVE));
                env.close();

                // check credential still present
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] == subject.human() &&
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
                env(jv, Ter(temBAD_FEE));

                testcase("CredentialsAccept fail, lsfAccepted already set.");
                env(credentials::accept(subject, issuer, credType));
                env.close();
                env(credentials::accept(subject, issuer, credType), Ter(tecDUPLICATE));
                env.close();

                // check credential still present
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] == subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }

            {
                char const credType2[] = "efghi";

                testcase("CredentialsAccept fail, expired credentials.");
                auto jv = credentials::create(subject, issuer, credType2);
                uint32_t const t =
                    env.current()->header().parentCloseTime.time_since_epoch().count();
                jv[sfExpiration.jsonName] = t;
                env(jv);
                env.close();

                // credentials are expired now
                env(credentials::accept(subject, issuer, credType2), Ter(tecEXPIRED));
                env.close();

                // check that expired credentials were deleted
                auto const jDelCred = credentials::ledgerEntry(env, subject, issuer, credType2);
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
                env(acctdelete(issuer, other), Fee(acctDelFee));

                // can't accept - no issuer account
                jv = credentials::accept(subject, issuer, credType);
                env(jv, Ter(tecNO_ISSUER));
                env.close();

                // check that expired credentials were deleted
                auto const jDelCred = credentials::ledgerEntry(env, subject, issuer, credType);
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

        char const credType[] = "abcde";
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
                env(credentials::deleteCred(subject, subject, issuer, credType), Ter(tecNO_ENTRY));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid Subject account.");
                auto jv = credentials::deleteCred(subject, subject, issuer, credType);
                jv[jss::Subject] = to_string(xrpAccount());
                env(jv, Ter(temINVALID_ACCOUNT_ID));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid Issuer account.");
                auto jv = credentials::deleteCred(subject, subject, issuer, credType);
                jv[jss::Issuer] = to_string(xrpAccount());
                env(jv, Ter(temINVALID_ACCOUNT_ID));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid credentialType param.");
                auto jv = credentials::deleteCred(subject, subject, issuer, "");
                env(jv, Ter(temMALFORMED));
            }

            {
                char const credType2[] = "fghij";

                env(credentials::create(subject, issuer, credType2));
                env.close();

                // Other account can't delete credentials without expiration
                env(credentials::deleteCred(other, subject, issuer, credType2),
                    Ter(tecNO_PERMISSION));
                env.close();

                // check credential still present
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType2);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] == subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType2)));
            }

            {
                testcase("CredentialsDelete fail, time not expired yet.");

                auto jv = credentials::create(subject, issuer, credType);
                // current time in XRPL epoch + 1000s
                uint32_t const t =
                    env.current()->header().parentCloseTime.time_since_epoch().count() + 1000;
                jv[sfExpiration.jsonName] = t;
                env(jv);
                env.close();

                // Other account can't delete credentials that not expired
                env(credentials::deleteCred(other, subject, issuer, credType),
                    Ter(tecNO_PERMISSION));
                env.close();

                // check credential still present
                auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
                BEAST_EXPECT(
                    jle.isObject() && jle.isMember(jss::result) &&
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node) &&
                    jle[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
                    jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
                    jle[jss::result][jss::node][jss::Subject] == subject.human() &&
                    jle[jss::result][jss::node]["CredentialType"] ==
                        strHex(std::string_view(credType)));
            }

            {
                testcase("CredentialsDelete fail, no Issuer and Subject.");

                auto jv = credentials::deleteCred(subject, subject, issuer, credType);
                jv.removeMember(jss::Subject);
                jv.removeMember(jss::Issuer);
                env(jv, Ter(temMALFORMED));
                env.close();
            }

            {
                testcase("CredentialsDelete fail, invalid fee.");

                auto jv = credentials::deleteCred(subject, subject, issuer, credType);
                jv[jss::Fee] = -1;
                env(jv, Ter(temBAD_FEE));
                env.close();
            }

            {
                testcase("deleteSLE fail, bad SLE.");
                auto view =
                    std::make_shared<ApplyViewImpl>(env.current().get(), ApplyFlags::TapNone);
                auto ter = xrpl::credentials::deleteSLE(*view, {}, env.journal);
                BEAST_EXPECT(ter == tecNO_ENTRY);
            }
        }
    }

    void
    testFeatureFailed(FeatureBitset features)
    {
        using namespace test::jtx;

        char const credType[] = "abcde";
        Account const issuer{"issuer"};
        Account const subject{"subject"};

        {
            using namespace jtx;
            Env env{*this, features};

            env.fund(XRP(5000), subject, issuer);
            env.close();

            {
                testcase("Credentials fail, Feature is not enabled.");
                env(credentials::create(subject, issuer, credType), Ter(temDISABLED));
                env(credentials::accept(subject, issuer, credType), Ter(temDISABLED));
                env(credentials::deleteCred(subject, subject, issuer, credType), Ter(temDISABLED));
            }
        }
    }

    void
    testRPC()
    {
        using namespace test::jtx;

        char const credType[] = "abcde";
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
                json::Value params;
                params[jss::account] = subject.human();
                auto const jv = env.rpc("json", "account_tx", to_string(params))[jss::result];

                BEAST_EXPECT(jv[jss::transactions].size() == 4);
                auto const& tx0(jv[jss::transactions][0u][jss::tx]);
                BEAST_EXPECT(tx0[jss::TransactionType] == jss::CredentialAccept);
                auto const& tx1(jv[jss::transactions][1u][jss::tx]);
                BEAST_EXPECT(tx1[jss::TransactionType] == jss::CredentialCreate);
                txHash0 = tx0[jss::hash].asString();
                txHash1 = tx1[jss::hash].asString();
            }

            {
                json::Value params;
                params[jss::account] = issuer.human();
                auto const jv = env.rpc("json", "account_tx", to_string(params))[jss::result];

                BEAST_EXPECT(jv[jss::transactions].size() == 4);
                auto const& tx0(jv[jss::transactions][0u][jss::tx]);
                BEAST_EXPECT(tx0[jss::TransactionType] == jss::CredentialAccept);
                auto const& tx1(jv[jss::transactions][1u][jss::tx]);
                BEAST_EXPECT(tx1[jss::TransactionType] == jss::CredentialCreate);

                BEAST_EXPECT(txHash0 == tx0[jss::hash].asString());
                BEAST_EXPECT(txHash1 == tx1[jss::hash].asString());
            }

            testcase("account_objects");
            std::string objectIdx;
            {
                json::Value params;
                params[jss::account] = subject.human();
                auto jv = env.rpc("json", "account_objects", to_string(params))[jss::result];

                BEAST_EXPECT(jv[jss::account_objects].size() == 1);
                auto const& object(jv[jss::account_objects][0u]);

                BEAST_EXPECT(object["LedgerEntryType"].asString() == jss::Credential);
                objectIdx = object[jss::index].asString();
            }

            {
                json::Value params;
                params[jss::account] = issuer.human();
                auto jv = env.rpc("json", "account_objects", to_string(params))[jss::result];

                BEAST_EXPECT(jv[jss::account_objects].size() == 1);
                auto const& object(jv[jss::account_objects][0u]);

                BEAST_EXPECT(object["LedgerEntryType"].asString() == jss::Credential);
                BEAST_EXPECT(objectIdx == object[jss::index].asString());
            }
        }
    }

    void
    testFlags(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const enabled = features[fixInvalidTxFlags];
        testcase(std::string("Test flag, fix ") + (enabled ? "enabled" : "disabled"));

        char const credType[] = "abcde";
        Account const issuer{"issuer"};
        Account const subject{"subject"};

        {
            using namespace jtx;
            Env env{*this, features};

            env.fund(XRP(5000), subject, issuer);
            env.close();

            {
                Ter const expected(enabled ? TER(temINVALID_FLAG) : TER(tesSUCCESS));
                env(credentials::create(subject, issuer, credType),
                    Txflags(tfTransferable),
                    expected);
                env(credentials::accept(subject, issuer, credType),
                    Txflags(tfSellNFToken),
                    expected);
                env(credentials::deleteCred(subject, subject, issuer, credType),
                    Txflags(tfPassive),
                    expected);
            }
        }
    }

    void
    testRemoveExpiredCorruption(FeatureBitset features)
    {
        bool const fixEnabled = features[fixCleanup3_1_3];
        testcase(
            "removeExpired ignores deleteSLE failure " +
            (fixEnabled ? std::string(" after fix") : std::string(" before fix")));

        using namespace test::jtx;

        char const credType[] = "abcde";
        Account const issuer{"issuer"};
        Account const subject{"subject"};
        Account const becky{"becky"};

        Env env{*this, features};
        env.fund(XRP(10000), issuer, subject, becky);
        env.close();

        // Create credential with short expiration
        auto jv = credentials::create(subject, issuer, credType);
        uint32_t const expiration =
            env.current()->header().parentCloseTime.time_since_epoch().count() + 40;
        jv[sfExpiration.jsonName] = expiration;
        env(jv);
        env.close();

        auto const credLE = credentials::ledgerEntry(env, subject, issuer, credType);
        std::string const credIdx = credLE[jss::result][jss::index].asString();

        // Subject accepts the credential
        env(credentials::accept(subject, issuer, credType));
        env.close();

        // Build the credential keylet
        auto const credKeylet =
            keylet::credential(subject.id(), issuer.id(), Slice(credType, std::strlen(credType)));

        // Verify credential exists and is accepted
        {
            auto const sleCred = env.current()->read(credKeylet);
            BEAST_EXPECT(sleCred && sleCred->getFlags() & lsfAccepted);
        }

        // Create DepositPreauth
        env(deposit::authCredentials(becky, {{subject, credType}}));
        env.close();
        // env();
        auto jtx = env.jt(pay(subject, becky, XRP(100)), credentials::Ids({credIdx}));
        if (!BEAST_EXPECT(jtx.stx))
            return;
        auto const stx = std::make_shared<STTx>(*jtx.stx);

        // Create PermissionedDomain
        env(pdomain::setTx(becky, {{issuer, credType}}));
        env.close();
        auto const objects = pdomain::getObjects(becky, env);
        if (!BEAST_EXPECT(!objects.empty()))
            return;
        auto const domain = objects.begin()->first;

        using namespace std::chrono_literals;
        env.close(50s);

        // Verify time has advanced past expiration
        {
            auto const sleCred = env.current()->read(credKeylet);
            BEAST_EXPECT(
                sleCred &&
                xrpl::credentials::checkExpired(*sleCred, env.current()->header().parentCloseTime));
        }

        // Create an ApplyViewImpl on top of the current closed ledger
        // and corrupt it by erasing the issuer's account SLE
        auto const open = env.current();
        ApplyViewImpl av(&*open, TapNone);

        // Erase the issuer's account to simulate ledger corruption
        auto sleIssuer = av.peek(keylet::account(issuer.id()));
        if (!BEAST_EXPECT(sleIssuer))
            return;
        av.erase(sleIssuer);
        BEAST_EXPECT(!av.exists(keylet::account(issuer.id())));

        // Credential still exists before removeExpired
        BEAST_EXPECT(av.exists(credKeylet));

        // Call removeExpired on the corrupted view
        STVector256 credHashes;
        credHashes.pushBack(credKeylet.key);
        beast::Journal const j{beast::Journal::getNullSink()};

        auto const dpTer = xrpl::verifyDepositPreauth(*stx, av, subject, becky, {}, j);
        auto sleCredAfter = av.read(credKeylet);
        BEAST_EXPECT(sleCredAfter && (sleCredAfter->getFlags() & lsfAccepted));

        auto const domTer = xrpl::verifyValidDomain(av, subject.id(), domain, j);
        sleCredAfter = av.read(credKeylet);
        BEAST_EXPECT(sleCredAfter && (sleCredAfter->getFlags() & lsfAccepted));

        if (fixEnabled)
        {
            // removeExpired returns error, cred wasn't deleted
            BEAST_EXPECT(dpTer == tecINTERNAL);
            BEAST_EXPECT(domTer == tecINTERNAL);
        }
        else
        {
            // removeExpired returns true (claims it found & deleted expired
            // creds)
            BEAST_EXPECT(dpTer == tecEXPIRED);
            BEAST_EXPECT(isTesSuccess(domTer));
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        testSuccessful(all);
        testCredentialsDelete(all);
        testCreateFailed(all);
        testCreateFailed(all - fixDirectoryLimit);
        testAcceptFailed(all);
        testDeleteFailed(all);
        testFeatureFailed(all - featureCredentials);
        testFlags(all - fixInvalidTxFlags);
        testFlags(all);
        testRPC();

        testRemoveExpiredCorruption(all - fixCleanup3_1_3);
        testRemoveExpiredCorruption(all | fixCleanup3_1_3);
    }
};

BEAST_DEFINE_TESTSUITE(Credentials, app, xrpl);

}  // namespace xrpl::test
