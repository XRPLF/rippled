#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/credentials.h>
#include <test/jtx/domain.h>
#include <test/jtx/fee.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/ledgerStateFix.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

using namespace jtx;

class PermissionedDEX_test : public beast::unit_test::Suite
{
    [[nodiscard]] static bool
    offerExists(Env const& env, Account const& account, std::uint32_t offerSeq)
    {
        return static_cast<bool>(env.le(keylet::offer(account.id(), offerSeq)));
    }

    [[nodiscard]] static bool
    checkOffer(
        Env const& env,
        Account const& account,
        std::uint32_t offerSeq,
        STAmount const& takerPays,
        STAmount const& takerGets,
        uint32_t const flags = 0,
        bool const domainOffer = false)
    {
        auto offerInDir = [&](uint256 const& directory,
                              uint64_t const pageIndex,
                              std::optional<uint256> domain = std::nullopt) -> bool {
            auto const page = env.le(keylet::page(directory, pageIndex));
            if (!page)
                return false;

            if (domain != (*page)[~sfDomainID])
                return false;

            auto const& indexes = page->getFieldV256(sfIndexes);
            for (auto const& index : indexes)
            {
                if (index == keylet::offer(account, offerSeq).key)
                    return true;
            }

            return false;
        };

        auto const sle = env.le(keylet::offer(account.id(), offerSeq));
        if (!sle)
            return false;
        if (sle->getFieldAmount(sfTakerGets) != takerGets)
            return false;
        if (sle->getFieldAmount(sfTakerPays) != takerPays)
            return false;
        if (sle->getFlags() != flags)
            return false;
        if (domainOffer && !sle->isFieldPresent(sfDomainID))
            return false;
        if (!domainOffer && sle->isFieldPresent(sfDomainID))
            return false;
        if (!offerInDir(
                sle->getFieldH256(sfBookDirectory),
                sle->getFieldU64(sfBookNode),
                (*sle)[~sfDomainID]))
            return false;

        if (sle->isFlag(lsfHybrid))
        {
            if (!sle->isFieldPresent(sfDomainID))
                return false;
            if (!sle->isFieldPresent(sfAdditionalBooks))
                return false;
            if (sle->getFieldArray(sfAdditionalBooks).size() != 1)
                return false;

            auto const& additionalBookDirs = sle->getFieldArray(sfAdditionalBooks);

            for (auto const& bookDir : additionalBookDirs)
            {
                auto const& dirIndex = bookDir.getFieldH256(sfBookDirectory);
                auto const& dirNode = bookDir.getFieldU64(sfBookNode);

                // the directory is for the open order book, so the dir
                // doesn't have domainID
                if (!offerInDir(dirIndex, dirNode, std::nullopt))
                    return false;
            }
        }
        else
        {
            if (sle->isFieldPresent(sfAdditionalBooks))
                return false;
        }

        return true;
    }

    static uint256
    getBookDirKey(Book const& book, STAmount const& takerPays, STAmount const& takerGets)
    {
        return keylet::quality(keylet::kBook(book), getRate(takerGets, takerPays)).key;
    }

    static std::optional<uint256>
    getDefaultOfferDirKey(Env const& env, Account const& account, std::uint32_t offerSeq)
    {
        if (auto const sle = env.le(keylet::offer(account.id(), offerSeq)))
            return Keylet(ltDIR_NODE, (*sle)[sfBookDirectory]).key;

        return {};
    }

    [[nodiscard]] static bool
    checkDirectorySize(Env const& env, uint256 directory, std::uint32_t dirSize)
    {
        std::optional<std::uint64_t> pageIndex{0};
        std::uint32_t dirCnt = 0;

        do
        {
            auto const page = env.le(
                keylet::page(directory, *pageIndex));  // NOLINT(bugprone-unchecked-optional-access)
            if (!page)
                break;

            pageIndex = (*page)[~sfIndexNext];
            dirCnt += (*page)[sfIndexes].size();

        } while (pageIndex.value_or(0) != 0u);

        return dirCnt == dirSize;
    }

    void
    testOfferCreate(FeatureBitset features)
    {
        testcase("OfferCreate");

        // test preflight
        {
            Env env(*this, features - featurePermissionedDEX);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)), Domain(domainID), Ter(temDISABLED));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();
        }

        // test preflight - malformed DomainID being zero
        // Only test this with fixCleanup3_2_0 enabled. Without the fix,
        // an assert-enabled build can crash when Ledger::read() receives
        // a zero-key PermissionedDomain keylet.
        if (features[fixCleanup3_2_0])
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob_, XRP(10), USD(10)), Domain(uint256{}), Ter(temMALFORMED));
            env.close();
        }

        // preclaim - someone outside of the domain cannot create domain offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            env(offer(devin, XRP(10), USD(10)), Domain(domainID), Ter(tecNO_PERMISSION));
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            // devin still cannot create offer since he didn't accept credential
            env(offer(devin, XRP(10), USD(10)), Domain(domainID), Ter(tecNO_PERMISSION));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            env(offer(devin, XRP(10), USD(10)), Domain(domainID));
            env.close();
        }

        // preclaim - someone with expired cred cannot create domain offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            auto jv = credentials::create(devin, domainOwner, credType);
            uint32_t const t = env.current()->header().parentCloseTime.time_since_epoch().count();
            jv[sfExpiration.jsonName] = t + 20;
            env(jv);

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can still create offer while his cred is not expired
            env(offer(devin, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // time advance
            env.close(std::chrono::seconds(20));

            // devin cannot create offer with expired cred
            env(offer(devin, XRP(10), USD(10)), Domain(domainID), Ter(tecNO_PERMISSION));
            env.close();
        }

        // preclaim - cannot create an offer in a non existent domain
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);
            uint256 const badDomain{
                "F10D0CC9A0F9A3CBF585B80BE09A186483668FDBDD39AA7E3370F3649CE134"
                "E5"};

            env(offer(bob, XRP(10), USD(10)), Domain(badDomain), Ter(tecNO_PERMISSION));
            env.close();
        }

        // apply - offer can be created even if takergets issuer is not in
        // domain
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(credentials::deleteCred(domainOwner, gw, domainOwner, credType));
            env.close();

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));
        }

        // apply - offer can be created even if takerpays issuer is not in
        // domain
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(credentials::deleteCred(domainOwner, gw, domainOwner, credType));
            env.close();

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), XRP(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, USD(10), XRP(10), 0, true));
        }

        // apply - two domain offers cross with each other
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            // a non domain offer cannot cross with domain offer
            env(offer(carol, USD(10), XRP(10)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));

            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
        }

        // apply - create lots of domain offers
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            std::vector<std::uint32_t> offerSeqs;
            offerSeqs.reserve(100);

            for (size_t i = 0; i <= 100; i++)
            {
                auto const bobOfferSeq{env.seq(bob)};
                offerSeqs.emplace_back(bobOfferSeq);

                env(offer(bob, XRP(10), USD(10)), Domain(domainID));
                env.close();
                BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));
            }

            for (auto const offerSeq : offerSeqs)
            {
                env(offerCancel(bob, offerSeq));
                env.close();
                BEAST_EXPECT(!offerExists(env, bob, offerSeq));
            }
        }
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        // test preflight - without enabling featurePermissionedDEX amendment
        {
            Env env(*this, features - featurePermissionedDEX);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(pay(bob, alice, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(temDISABLED));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();

            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            env(pay(bob, alice, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }

        // test preflight - malformed DomainID being zero
        // Only test this with fixCleanup3_2_0 enabled. Without the fix,
        // an assert-enabled build can crash when Ledger::read() receives
        // a zero-key PermissionedDomain keylet.
        if (features[fixCleanup3_2_0])
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(pay(bob_, alice_, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(uint256{}),
                Ter(temMALFORMED));
            env.close();
        }

        // preclaim - cannot send payment with non existent domain
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);
            uint256 const badDomain{
                "F10D0CC9A0F9A3CBF585B80BE09A186483668FDBDD39AA7E3370F3649CE134"
                "E5"};

            env(pay(bob, alice, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(badDomain),
                Ter(tecNO_PERMISSION));
            env.close();
        }

        // preclaim - payment with non-domain destination fails
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            // devin is not part of domain
            env(pay(alice, devin, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecNO_PERMISSION));
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            // devin has not yet accepted cred
            env(pay(alice, devin, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecNO_PERMISSION));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can now receive payment after he is in domain
            env(pay(alice, devin, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }

        // preclaim - non-domain sender cannot send payment
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            // devin tries to send domain payment
            env(pay(devin, alice, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecNO_PERMISSION));
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            // devin has not yet accepted cred
            env(pay(devin, alice, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecNO_PERMISSION));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can now send payment after he is in domain
            env(pay(devin, alice, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }

        // apply - domain owner can always send and receive domain payment
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // domain owner can always be destination
            env(pay(alice, domainOwner, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();

            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // domain owner can send
            env(pay(domainOwner, alice, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("Book step");

        // test domain cross currency payment consuming one offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // create a regular offer without domain
            auto const regularOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, XRP(10), USD(10)));

            auto const regularDirKey = getDefaultOfferDirKey(env, bob, regularOfferSeq);
            BEAST_EXPECT(regularDirKey);
            BEAST_EXPECT(checkDirectorySize(
                env, *regularDirKey, 1));  // NOLINT(bugprone-unchecked-optional-access)

            // a domain payment cannot consume regular offers
            env(pay(alice, carol, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();

            // create a domain offer
            auto const domainOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, domainOfferSeq, XRP(10), USD(10), 0, true));

            auto const domainDirKey = getDefaultOfferDirKey(env, bob, domainOfferSeq);
            BEAST_EXPECT(domainDirKey);
            BEAST_EXPECT(checkDirectorySize(
                env, *domainDirKey, 1));  // NOLINT(bugprone-unchecked-optional-access)

            // cross-currency permissioned payment consumed
            // domain offer instead of regular offer
            env(pay(alice, carol, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
            BEAST_EXPECT(!offerExists(env, bob, domainOfferSeq));
            BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, XRP(10), USD(10)));

            // domain directory is empty
            BEAST_EXPECT(checkDirectorySize(
                env, *domainDirKey, 0));  // NOLINT(bugprone-unchecked-optional-access)
            BEAST_EXPECT(checkDirectorySize(
                env, *regularDirKey, 1));  // NOLINT(bugprone-unchecked-optional-access)
        }

        // test domain payment consuming two offers in the path
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const eur = gw["EUR"];
            env.trust(eur(1000), alice);
            env.close();
            env.trust(eur(1000), bob);
            env.close();
            env.trust(eur(1000), carol);
            env.close();
            env(pay(gw, bob, eur(100)));
            env.close();

            // create XRP/USD domain offer
            auto const usdOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, true));

            // payment fail because there isn't eur offer
            env(pay(alice, carol, eur(10)),
                Path(~USD, ~eur),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, true));

            // bob creates a regular USD/EUR offer
            auto const regularOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), eur(10)));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, USD(10), eur(10)));

            // alice tries to pay again, but still fails because the regular
            // offer cannot be consumed
            env(pay(alice, carol, eur(10)),
                Path(~USD, ~eur),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();

            // bob creates a domain USD/EUR offer
            auto const eurOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), eur(10)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, eurOfferSeq, USD(10), eur(10), 0, true));

            // alice successfully consume two domain offers: xrp/usd and usd/eur
            env(pay(alice, carol, eur(5)), Sendmax(XRP(5)), Domain(domainID), Path(~USD, ~eur));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, usdOfferSeq, XRP(5), USD(5), 0, true));
            BEAST_EXPECT(checkOffer(env, bob, eurOfferSeq, USD(5), eur(5), 0, true));

            // alice successfully consume two domain offers and deletes them
            // we compute path this time using `paths`
            env(pay(alice, carol, eur(5)), Sendmax(XRP(5)), Domain(domainID), Paths(XRP));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob, usdOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob, eurOfferSeq));

            // regular offer is not consumed
            BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, USD(10), eur(10)));
        }

        // domain payment cannot consume offer from another domain
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // Fund devin and create USD trustline
            Account const badDomainOwner("badDomainOwner");
            Account const devin("devin");
            env.fund(XRP(1000), badDomainOwner, devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            auto const badCredType = "badCred";
            pdomain::Credentials const credentials{
                {.issuer = badDomainOwner, .credType = badCredType}};
            env(pdomain::setTx(badDomainOwner, credentials));

            auto objects = pdomain::getObjects(badDomainOwner, env);
            auto const badDomainID = objects.begin()->first;

            env(credentials::create(devin, badDomainOwner, badCredType));
            env.close();
            env(credentials::accept(devin, badDomainOwner, badCredType));

            // devin creates a domain offer in another domain
            env(offer(devin, XRP(10), USD(10)), Domain(badDomainID));
            env.close();

            // domain payment can't consume an offer from another domain
            env(pay(alice, carol, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();

            // bob creates an offer under the right domain
            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));

            // domain payment now consumes from the right domain
            env(pay(alice, carol, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
        }

        // sanity check: devin, who is part of the domain but doesn't have a
        // trustline with USD issuer, can successfully make a payment using
        // offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // fund devin but don't create a USD trustline with gateway
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // successful payment because offer is consumed
            env(pay(devin, alice, USD(10)), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }

        // offer becomes unfunded when offer owner's cred expires
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            auto jv = credentials::create(devin, domainOwner, credType);
            uint32_t const t = env.current()->header().parentCloseTime.time_since_epoch().count();
            jv[sfExpiration.jsonName] = t + 20;
            env(jv);

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can still create offer while his cred is not expired
            auto const offerSeq{env.seq(devin)};
            env(offer(devin, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // devin's offer can still be consumed while his cred isn't expired
            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, devin, offerSeq, XRP(5), USD(5), 0, true));

            // advance time
            env.close(std::chrono::seconds(20));

            // devin's offer is unfunded now due to expired cred
            env(pay(alice, carol, USD(5)),
                Path(~USD),
                Sendmax(XRP(5)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(checkOffer(env, devin, offerSeq, XRP(5), USD(5), 0, true));
        }

        // offer becomes unfunded when offer owner's cred is removed
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const offerSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // bob's offer can still be consumed while his cred exists
            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, offerSeq, XRP(5), USD(5), 0, true));

            // remove bob's cred
            env(credentials::deleteCred(domainOwner, bob, domainOwner, credType));
            env.close();

            // bob's offer is unfunded now due to expired cred
            env(pay(alice, carol, USD(5)),
                Path(~USD),
                Sendmax(XRP(5)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, offerSeq, XRP(5), USD(5), 0, true));
        }
    }

    void
    testRippling(FeatureBitset features)
    {
        testcase("Rippling");

        // test a non-domain account can still be part of rippling in a domain
        // payment. If the domain wishes to control who is allowed to ripple
        // through, they should set the rippling individually
        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        auto const eura = alice["EUR"];
        auto const eurb = bob["EUR"];

        env.trust(eura(100), bob);
        env.trust(eurb(100), carol);
        env.close();

        // remove bob from domain
        env(credentials::deleteCred(domainOwner, bob, domainOwner, credType));
        env.close();

        // alice can still ripple through bob even though he's not part
        // of the domain, this is intentional
        env(pay(alice, carol, eurb(10)), Paths(eura), Domain(domainID));
        env.close();
        env.require(Balance(bob, eura(10)), Balance(carol, eurb(10)));

        // carol sets no ripple on bob
        env(trust(carol, bob["EUR"](0), bob, tfSetNoRipple));
        env.close();

        // payment no longer works because carol has no ripple on bob
        env(pay(alice, carol, eurb(5)), Paths(eura), Domain(domainID), Ter(tecPATH_DRY));
        env.close();
        env.require(Balance(bob, eura(10)), Balance(carol, eurb(10)));
    }

    void
    testOfferTokenIssuerInDomain(FeatureBitset features)
    {
        testcase("Offer token issuer in domain");

        // whether the issuer is in the domain should NOT affect whether an
        // offer can be consumed in domain payment
        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        // create an xrp/usd offer with usd as takergets
        auto const bobOffer1Seq{env.seq(bob)};
        env(offer(bob, XRP(10), USD(10)), Domain(domainID));
        env.close();

        // create an usd/xrp offer with usd as takerpays
        auto const bobOffer2Seq{env.seq(bob)};
        env(offer(bob, USD(10), XRP(10)), Domain(domainID), Txflags(tfPassive));
        env.close();

        BEAST_EXPECT(checkOffer(env, bob, bobOffer1Seq, XRP(10), USD(10), 0, true));
        BEAST_EXPECT(checkOffer(env, bob, bobOffer2Seq, USD(10), XRP(10), lsfPassive, true));

        // remove gateway from domain
        env(credentials::deleteCred(domainOwner, gw, domainOwner, credType));
        env.close();

        // payment succeeds even if issuer is not in domain
        // xrp/usd offer is consumed
        env(pay(alice, carol, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
        env.close();
        BEAST_EXPECT(!offerExists(env, bob, bobOffer1Seq));

        // payment succeeds even if issuer is not in domain
        // usd/xrp offer is consumed
        env(pay(alice, carol, XRP(10)), Path(~XRP), Sendmax(USD(10)), Domain(domainID));
        env.close();
        BEAST_EXPECT(!offerExists(env, bob, bobOffer2Seq));
    }

    void
    testRemoveUnfundedOffer(FeatureBitset features)
    {
        testcase("Remove unfunded offer");

        // checking that an unfunded offer will be implicitly removed by a
        // successful payment tx
        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        auto const aliceOfferSeq{env.seq(alice)};
        env(offer(alice, XRP(100), USD(100)), Domain(domainID));
        env.close();

        auto const bobOfferSeq{env.seq(bob)};
        env(offer(bob, XRP(20), USD(20)), Domain(domainID));
        env.close();

        BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(20), USD(20), 0, true));
        BEAST_EXPECT(checkOffer(env, alice, aliceOfferSeq, XRP(100), USD(100), 0, true));

        auto const domainDirKey = getDefaultOfferDirKey(env, bob, bobOfferSeq);
        BEAST_EXPECT(domainDirKey);
        BEAST_EXPECT(checkDirectorySize(
            env, *domainDirKey, 2));  // NOLINT(bugprone-unchecked-optional-access)

        // remove alice from domain and thus alice's offer becomes unfunded
        env(credentials::deleteCred(domainOwner, alice, domainOwner, credType));
        env.close();

        env(pay(gw, carol, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
        env.close();

        BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));

        // alice's unfunded offer is removed implicitly
        BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
        BEAST_EXPECT(checkDirectorySize(
            env, *domainDirKey, 1));  // NOLINT(bugprone-unchecked-optional-access)
    }

    void
    testAmmNotUsed(FeatureBitset features)
    {
        testcase("AMM not used");

        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);
        AMM const amm(env, alice, XRP(10), USD(50));

        // a domain payment isn't able to consume AMM
        env(pay(bob, carol, USD(5)),
            Path(~USD),
            Sendmax(XRP(5)),
            Domain(domainID),
            Ter(tecPATH_PARTIAL));
        env.close();

        // a non domain payment can use AMM
        env(pay(bob, carol, USD(5)), Path(~USD), Sendmax(XRP(5)));
        env.close();

        // USD amount in AMM is changed
        auto [xrp, usd, lpt] = amm.balances(XRP, USD);
        BEAST_EXPECT(usd == USD(45));
    }

    void
    testHybridOfferCreate(FeatureBitset features)
    {
        testcase("Hybrid offer create");

        // test preflight - invalid hybrid flag
        {
            Env env(*this, features - featurePermissionedDEX);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)),
                Domain(domainID),
                Txflags(tfHybrid),
                Ter(temDISABLED));
            env.close();

            env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Ter(temINVALID_FLAG));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();

            // hybrid offer must have domainID
            env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Ter(temINVALID_FLAG));
            env.close();

            // hybrid offer must have domainID
            auto const offerSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, offerSeq, XRP(10), USD(10), lsfHybrid, true));
        }

        // apply - domain offer can cross with hybrid
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));
            BEAST_EXPECT(offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
        }

        // apply - open offer can cross with hybrid
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            BEAST_EXPECT(offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, bob) == 3);
            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));

            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)));
            env.close();

            BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
        }

        // apply - by default, hybrid offer tries to cross with offers in the
        // domain book
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            // hybrid offer auto crosses with domain offer
            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)), Domain(domainID), Txflags(tfHybrid));
            env.close();

            BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
        }

        // apply - hybrid offer does not automatically cross with open offers
        // because by default, it only tries to cross domain offers
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, false));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            // hybrid offer auto crosses with domain offer
            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)), Domain(domainID), Txflags(tfHybrid));
            env.close();

            BEAST_EXPECT(offerExists(env, alice, aliceOfferSeq));
            BEAST_EXPECT(offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, false));
            BEAST_EXPECT(checkOffer(env, alice, aliceOfferSeq, USD(10), XRP(10), lsfHybrid, true));
            BEAST_EXPECT(ownerCount(env, alice) == 3);
        }
    }

    void
    testHybridInvalidOffer(FeatureBitset features)
    {
        testcase("Hybrid invalid offer");

        // bob has a hybrid offer and then he is removed from the domain.
        // Domain payments must not consume the offer; regular open-book
        // payments follow the fixCleanup3_3_0 behavior checked below.
        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        auto const hybridOfferSeq{env.seq(bob)};
        env(offer(bob, XRP(50), USD(50)), Txflags(tfHybrid), Domain(domainID));
        env.close();

        // remove bob from domain
        env(credentials::deleteCred(domainOwner, bob, domainOwner, credType));
        env.close();

        // bob's hybrid offer is unfunded and can not be consumed in a domain
        // payment
        env(pay(alice, carol, USD(5)),
            Path(~USD),
            Sendmax(XRP(5)),
            Domain(domainID),
            Ter(tecPATH_PARTIAL));
        env.close();
        BEAST_EXPECT(checkOffer(env, bob, hybridOfferSeq, XRP(50), USD(50), lsfHybrid, true));

        if (features[fixCleanup3_3_0])
        {
            // Post-fixCleanup3_3_0: hybrid offer can still be consumed via a regular
            // open-book payment even though the domain credential was revoked.
            auto const carolBalBefore = env.balance(carol, USD);
            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)));
            env.close();
            BEAST_EXPECT(env.balance(carol, USD) - carolBalBefore == USD(5));
            BEAST_EXPECT(checkOffer(env, bob, hybridOfferSeq, XRP(45), USD(45), lsfHybrid, true));

            // create a regular offer alongside the hybrid one
            auto const regularOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, XRP(10), USD(10)));

            auto const sleHybridOffer = env.le(keylet::offer(bob.id(), hybridOfferSeq));
            if (!BEAST_EXPECT(sleHybridOffer))
                return;
            auto const openDir =
                sleHybridOffer->getFieldArray(sfAdditionalBooks)[0].getFieldH256(sfBookDirectory);
            // both offers are in the open book directory
            BEAST_EXPECT(checkDirectorySize(env, openDir, 2));

            // A regular payment crosses the hybrid offer first (FIFO, older
            // offer), then stops; the regular offer is untouched.
            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, hybridOfferSeq, XRP(40), USD(40), lsfHybrid, true));
            BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, XRP(10), USD(10)));
            BEAST_EXPECT(checkDirectorySize(env, openDir, 2));
        }
        else
        {
            // Pre-fixCleanup3_3_0: the open-book traversal
            // also runs the offerInDomain eviction check, so the hybrid offer
            // is treated as unfunded and the regular payment fails.
            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)), Ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, hybridOfferSeq, XRP(50), USD(50), lsfHybrid, true));

            // create a regular offer
            auto const regularOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)));
            env.close();
            BEAST_EXPECT(offerExists(env, bob, regularOfferSeq));
            BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, XRP(10), USD(10)));

            auto const sleHybridOffer = env.le(keylet::offer(bob.id(), hybridOfferSeq));
            if (!BEAST_EXPECT(sleHybridOffer))
                return;
            auto const openDir =
                sleHybridOffer->getFieldArray(sfAdditionalBooks)[0].getFieldH256(sfBookDirectory);
            BEAST_EXPECT(checkDirectorySize(env, openDir, 2));

            // This payment crosses the regular offer and permanently evicts the
            // hybrid offer from the open book (since the payment succeeds, the
            // sandbox, including the hybrid eviction, is committed).
            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob, hybridOfferSeq));
            BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, XRP(5), USD(5)));
            BEAST_EXPECT(checkDirectorySize(env, openDir, 1));
        }
    }

    void
    testHybridBookStep(FeatureBitset features)
    {
        testcase("Hybrid book step");

        // both non domain and domain payments can consume hybrid offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const hybridOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, hybridOfferSeq, XRP(5), USD(5), lsfHybrid, true));

            // hybrid offer can't be consumed since bob is not in domain anymore
            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob, hybridOfferSeq));
        }

        // someone from another domain can't cross hybrid if they specified
        // wrong domainID
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // Fund accounts
            Account const badDomainOwner("badDomainOwner");
            Account const devin("devin");
            env.fund(XRP(1000), badDomainOwner, devin);
            env.close();

            auto const badCredType = "badCred";
            pdomain::Credentials const credentials{
                {.issuer = badDomainOwner, .credType = badCredType}};
            env(pdomain::setTx(badDomainOwner, credentials));

            auto objects = pdomain::getObjects(badDomainOwner, env);
            auto const badDomainID = objects.begin()->first;

            env(credentials::create(devin, badDomainOwner, badCredType));
            env.close();
            env(credentials::accept(devin, badDomainOwner, badCredType));
            env.close();

            auto const hybridOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            // other domains can't consume the offer
            env(pay(devin, badDomainOwner, USD(5)),
                Path(~USD),
                Sendmax(XRP(5)),
                Domain(badDomainID),
                Ter(tecPATH_DRY));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, hybridOfferSeq, XRP(10), USD(10), lsfHybrid, true));

            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, hybridOfferSeq, XRP(5), USD(5), lsfHybrid, true));

            // hybrid offer can't be consumed since bob is not in domain anymore
            env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob, hybridOfferSeq));
        }

        // test domain payment consuming two offers w/ hybrid offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const eur = gw["EUR"];
            env.trust(eur(1000), alice);
            env.close();
            env.trust(eur(1000), bob);
            env.close();
            env.trust(eur(1000), carol);
            env.close();
            env(pay(gw, bob, eur(100)));
            env.close();

            auto const usdOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, true));

            // payment fail because there isn't eur offer
            env(pay(alice, carol, eur(5)),
                Path(~USD, ~eur),
                Sendmax(XRP(5)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, true));

            // bob creates a hybrid eur offer
            auto const eurOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), eur(10)), Domain(domainID), Txflags(tfHybrid));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, eurOfferSeq, USD(10), eur(10), lsfHybrid, true));

            // alice successfully consume two domain offers: xrp/usd and usd/eur
            env(pay(alice, carol, eur(5)), Path(~USD, ~eur), Sendmax(XRP(5)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, usdOfferSeq, XRP(5), USD(5), 0, true));
            BEAST_EXPECT(checkOffer(env, bob, eurOfferSeq, USD(5), eur(5), lsfHybrid, true));
        }

        // test regular payment using a regular offer and a hybrid offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const eur = gw["EUR"];
            env.trust(eur(1000), alice);
            env.close();
            env.trust(eur(1000), bob);
            env.close();
            env.trust(eur(1000), carol);
            env.close();
            env(pay(gw, bob, eur(100)));
            env.close();

            // bob creates a regular usd offer
            auto const usdOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, false));

            // bob creates a hybrid eur offer
            auto const eurOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), eur(10)), Domain(domainID), Txflags(tfHybrid));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, eurOfferSeq, USD(10), eur(10), lsfHybrid, true));

            // alice successfully consume two offers: xrp/usd and usd/eur
            env(pay(alice, carol, eur(5)), Path(~USD, ~eur), Sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob, usdOfferSeq, XRP(5), USD(5), 0, false));
            BEAST_EXPECT(checkOffer(env, bob, eurOfferSeq, USD(5), eur(5), lsfHybrid, true));
        }
    }

    // Test that a hybrid offer remains crossable in the open book after the
    // owner's domain credential expires. A domain payment after expiry should
    // fail (domain book evicts the offer in its sandbox), but the open book
    // remains usable.
    void
    testHybridOpenBookAfterCredentialExpiry(FeatureBitset features)
    {
        testcase("Hybrid open book after credential expiry");

        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        Account const devin("devin");
        env.fund(XRP(100000), devin);
        env.close();
        env.trust(USD(1000), devin);
        env.close();
        env(pay(gw, devin, USD(100)));
        env.close();

        // Give devin a credential that expires far enough in the future to
        // survive the setup env.close() calls.
        auto jv = credentials::create(devin, domainOwner, credType);
        uint32_t const t = env.current()->header().parentCloseTime.time_since_epoch().count();
        jv[sfExpiration.jsonName] = t + 100;
        env(jv);
        env.close();
        env(credentials::accept(devin, domainOwner, credType));
        env.close();

        // Devin creates a hybrid offer: sell USD(10) for XRP(10).
        // The offer is placed in both the domain book and the open book.
        auto const hybridOfferSeq{env.seq(devin)};
        env(offer(devin, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
        env.close();

        BEAST_EXPECT(checkOffer(env, devin, hybridOfferSeq, XRP(10), USD(10), lsfHybrid, true));

        // A non-domain open-book payment partially crosses the offer while
        // devin's credential is still valid.
        auto carolBalance = env.balance(carol, USD);
        env(pay(alice, carol, USD(5)), Path(~USD), Sendmax(XRP(5)));
        env.close();
        BEAST_EXPECT(env.balance(carol, USD) - carolBalance == USD(5));
        BEAST_EXPECT(checkOffer(env, devin, hybridOfferSeq, XRP(5), USD(5), lsfHybrid, true));

        // Advance time so that devin's credential expires.
        env.close(std::chrono::seconds(100));

        // Confirm devin can no longer create domain offers.
        env(offer(devin, XRP(1), USD(1)), Domain(domainID), Ter(tecNO_PERMISSION));
        env.close();

        // The hybrid offer must still exist in the open book after expiry.
        BEAST_EXPECT(offerExists(env, devin, hybridOfferSeq));

        // A non-domain open-book payment must cross (not evict) the
        // remaining portion of devin's hybrid offer.
        carolBalance = env.balance(carol, USD);
        env(pay(alice, carol, USD(2)), Path(~USD), Sendmax(XRP(2)));
        env.close();

        // Carol received USD; the offer was crossed, not evicted.
        BEAST_EXPECT(env.balance(carol, USD) - carolBalance == USD(2));
        // Offer still exists with 3 USD / 3 XRP remaining.
        BEAST_EXPECT(checkOffer(env, devin, hybridOfferSeq, XRP(3), USD(3), lsfHybrid, true));

        // A domain payment now fails because the domain book evicts devin's
        // offer (his credential has expired).  The eviction is rolled back with
        // the failed sandbox, so the offer is NOT permanently removed.
        env(pay(alice, carol, USD(1)),
            Path(~USD),
            Sendmax(XRP(1)),
            Domain(domainID),
            Ter(tecPATH_PARTIAL));
        env.close();

        // Offer still intact in the open book; domain payment did not
        // permanently delete it.
        BEAST_EXPECT(checkOffer(env, devin, hybridOfferSeq, XRP(3), USD(3), lsfHybrid, true));

        // The open book can still fully consume the remaining portion.
        carolBalance = env.balance(carol, USD);
        env(pay(alice, carol, USD(3)), Path(~USD), Sendmax(XRP(3)));
        env.close();
        BEAST_EXPECT(env.balance(carol, USD) - carolBalance == USD(3));
        BEAST_EXPECT(!offerExists(env, devin, hybridOfferSeq));
    }

    void
    testHybridOfferDirectories(FeatureBitset features)
    {
        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        std::vector<std::uint32_t> offerSeqs;
        offerSeqs.reserve(100);

        Book const domainBook{Issue(XRP), Issue(USD), domainID};
        Book const openBook{Issue(XRP), Issue(USD), std::nullopt};

        auto const domainDir = getBookDirKey(domainBook, XRP(10), USD(10));
        auto const openDir = getBookDirKey(openBook, XRP(10), USD(10));

        size_t dirCnt = 100;

        for (size_t i = 1; i <= dirCnt; i++)
        {
            auto const bobOfferSeq{env.seq(bob)};
            offerSeqs.emplace_back(bobOfferSeq);
            env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            auto const sleOffer = env.le(keylet::offer(bob.id(), bobOfferSeq));
            BEAST_EXPECT(sleOffer);
            BEAST_EXPECT(sleOffer->getFieldH256(sfBookDirectory) == domainDir);
            BEAST_EXPECT(sleOffer->getFieldArray(sfAdditionalBooks).size() == 1);
            BEAST_EXPECT(
                sleOffer->getFieldArray(sfAdditionalBooks)[0].getFieldH256(sfBookDirectory) ==
                openDir);

            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));
            BEAST_EXPECT(checkDirectorySize(env, domainDir, i));
            BEAST_EXPECT(checkDirectorySize(env, openDir, i));
        }

        for (auto const offerSeq : offerSeqs)
        {
            env(offerCancel(bob, offerSeq));
            env.close();
            dirCnt--;
            BEAST_EXPECT(!offerExists(env, bob, offerSeq));
            BEAST_EXPECT(checkDirectorySize(env, domainDir, dirCnt));
            BEAST_EXPECT(checkDirectorySize(env, openDir, dirCnt));
        }
    }

    void
    testAutoBridge(FeatureBitset features)
    {
        testcase("Auto bridge");

        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);
        auto const eur = gw["EUR"];

        for (auto const& account : {alice, bob, carol})
        {
            env(trust(account, eur(10000)));
            env.close();
        }

        env(pay(gw, carol, eur(1)));
        env.close();

        auto const aliceOfferSeq{env.seq(alice)};
        auto const bobOfferSeq{env.seq(bob)};
        env(offer(alice, XRP(100), USD(1)), Domain(domainID));
        env(offer(bob, eur(1), XRP(100)), Domain(domainID));
        env.close();

        // carol's offer should cross bob and alice's offers due to auto
        // bridging
        auto const carolOfferSeq{env.seq(carol)};
        env(offer(carol, USD(1), eur(1)), Domain(domainID));
        env.close();

        BEAST_EXPECT(!offerExists(env, bob, aliceOfferSeq));
        BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
        BEAST_EXPECT(!offerExists(env, bob, carolOfferSeq));
    }

    void
    testHybridMalformedOffer(FeatureBitset features)
    {
        bool const fixEnabled = features[fixCleanup3_1_3];

        testcase << "Hybrid offer with empty AdditionalBooks"
                 << (fixEnabled ? " (fixCleanup3_1_3 enabled)" : " (fixCleanup3_1_3 disabled)");

        // offerInDomain has two code paths gated by fixCleanup3_1_3:
        //
        // pre-fix:  only rejects a hybrid offer when sfAdditionalBooks is
        //           entirely absent — an empty array (size 0) passes through.
        // post-fix: also rejects a hybrid offer whose sfAdditionalBooks array
        //           has size != 1 (i.e. 0 or >1 entries).
        //
        // We create a valid hybrid offer, then directly manipulate its SLE to
        // produce the size==0 case that cannot occur via normal transactions,
        // and verify that the two code paths produce the expected outcomes.
        //
        // Note: the PermissionedDEX invariant checker (ValidPermissionedDEX)
        // does not flag this malformation for ttPAYMENT — only for
        // ttOFFER_CREATE — so the without-fix payment completes as tesSUCCESS.

        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        // Create a valid hybrid offer (sfAdditionalBooks has exactly 1 entry)
        auto const bobOfferSeq{env.seq(bob)};
        env(offer(bob, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
        env.close();
        BEAST_EXPECT(offerExists(env, bob, bobOfferSeq));

        // Directly manipulate the offer SLE in the open ledger so that
        // sfAdditionalBooks is present but empty (size 0). This is the
        // malformed state that fixCleanup3_1_3 is designed to catch.
        auto const offerKey = keylet::offer(bob.id(), bobOfferSeq);
        env.app().getOpenLedger().modify([&offerKey](OpenView& view, beast::Journal) {
            auto const sle = view.read(offerKey);
            if (!sle)
                return false;
            auto replacement = std::make_shared<SLE>(*sle, sle->key());
            replacement->setFieldArray(sfAdditionalBooks, STArray{});
            view.rawReplace(replacement);
            return true;
        });

        if (fixEnabled)
        {
            // post-fixCleanup3_1_3: offerInDomain rejects the malformed
            // offer (size == 0), so no valid domain offer is found.
            env(pay(alice, carol, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
        }
        else
        {
            // pre-fixCleanup3_1_3: offerInDomain only checks for a missing
            // sfAdditionalBooks field; size == 0 passes through, so the
            // malformed offer is crossed and the payment succeeds.
            env(pay(alice, carol, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
        }
    }

    void
    testHybridOfferCrossingQuality(FeatureBitset features)
    {
        bool const fixEnabled = features[fixCleanup3_2_0];
        testcase << "Hybrid offer crossing quality"
                 << (fixEnabled ? " (fixCleanup3_2_0)" : " (pre-fix)");

        // Partially-crossed hybrid offer should have consistent quality
        // across both book directories.
        //
        // Steps:
        //   - Bob places a hybrid offer.
        //   - Alice places an opposing hybrid offer that partially crosses.
        //
        // Verify:
        //   - Domain-book key quality == its sfExchangeRate.
        //   - Post-fix: open-book key quality == domain-book key quality.
        //   - Pre-fix: open-book key quality != domain-book key quality
        //     (key used post-crossing rate, sfExchangeRate used pre-crossing).

        Env env(*this, features);
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
            PermissionedDEX(env);

        // Bob places a hybrid offer: TakerPays = XRP(100), TakerGets = USD(40)
        auto const bobOfferSeq{env.seq(bob_)};
        env(offer(bob_, XRP(100), USD(40)), Txflags(tfHybrid), Domain(domainID));
        env.close();
        BEAST_EXPECT(offerExists(env, bob_, bobOfferSeq));

        // Alice places a hybrid offer in the opposite direction that
        // partially crosses Bob's offer.
        // Alice: TakerPays = USD(100), TakerGets = XRP(300) (rate = 3 XRP/USD)
        // Bob's offer is at a better rate (2.5 XRP/USD) so crossing occurs.
        auto const aliceOfferSeq{env.seq(alice_)};
        env(offer(alice_, USD(100), XRP(300)), Txflags(tfHybrid), Domain(domainID));
        env.close();

        // After crossing, Alice's remaining offer should be placed.
        auto const sle = env.le(keylet::offer(alice_.id(), aliceOfferSeq));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->isFieldPresent(sfAdditionalBooks));
        BEAST_EXPECT(sle->getFieldArray(sfAdditionalBooks).size() == 1);

        auto const domainDirKey = sle->getFieldH256(sfBookDirectory);
        auto const openDirKey =
            sle->getFieldArray(sfAdditionalBooks)[0].getFieldH256(sfBookDirectory);

        auto const domainQuality = getQuality(domainDirKey);
        auto const openQuality = getQuality(openDirKey);

        // Read the directory SLEs and check sfExchangeRate vs key quality.
        auto const domainDirSle = env.le(Keylet(ltDIR_NODE, domainDirKey));
        auto const openDirSle = env.le(Keylet(ltDIR_NODE, openDirKey));
        BEAST_EXPECT(domainDirSle);
        BEAST_EXPECT(openDirSle);

        auto const domainExRate = domainDirSle->getFieldU64(sfExchangeRate);
        auto const openExRate = openDirSle->getFieldU64(sfExchangeRate);
        auto const preCrossingQuality = std::uint64_t{5623825668291712342ULL};
        auto const postCrossingQuality = std::uint64_t{5623825668291712341ULL};

        // Domain directory: sfExchangeRate should always match key quality
        // (both use the pre-crossing rate). Correct behavior.
        BEAST_EXPECT(domainQuality == preCrossingQuality);
        BEAST_EXPECT(domainExRate == preCrossingQuality);
        BEAST_EXPECT(domainExRate == domainQuality);

        if (fixEnabled)
        {
            // Correct behavior: both directory keys use the pre-crossing rate.
            BEAST_EXPECT(openQuality == preCrossingQuality);
            BEAST_EXPECT(domainQuality == openQuality);

            // sfExchangeRate matches key quality on both directories.
            BEAST_EXPECT(openExRate == preCrossingQuality);
            BEAST_EXPECT(openExRate == openQuality);
        }
        else
        {
            // Wrong legacy behavior: the open-book directory key uses the
            // post-crossing rate instead of the domain-book rate.
            BEAST_EXPECT(openQuality == postCrossingQuality);
            BEAST_EXPECT(domainQuality != openQuality);

            // The open-book sfExchangeRate still uses the pre-crossing rate,
            // so it no longer matches the actual quality encoded in the
            // open-book directory key.
            BEAST_EXPECT(openExRate == preCrossingQuality);
            BEAST_EXPECT(openExRate != openQuality);
            BEAST_EXPECT(openExRate == domainQuality);
        }
    }

    void
    testBookExchangeRateFix(FeatureBitset features)
    {
        testcase("LedgerStateFix BookExchangeRate");

        // Use the pre-fix path to create a hybrid offer with a mismatched
        // sfExchangeRate, then apply LedgerStateFix to correct it.
        //
        // Steps:
        //   - Create a partially-crossed hybrid offer (pre-fixCleanup3_2_0)
        //     so the open-book directory has wrong sfExchangeRate.
        //   - Re-enable fixCleanup3_2_0 and submit a LedgerStateFix to
        //     repair the open-book directory's sfExchangeRate.
        //
        // Verify:
        //   - Before fix: sfExchangeRate != getQuality(key).
        //   - After fix: sfExchangeRate == getQuality(key).

        {
            // Amendment gate: BookExchangeRate fixes require fixCleanup3_2_0.
            Env env(*this, features - fixCleanup3_2_0);
            Account const carol{"carol"};

            env.fund(XRP(1000), carol);
            env.close();

            env(ledgerStateFix::bookExchangeRate(carol, uint256{1}), Ter(temDISABLED));
        }

        {
            // Preflight check: BookExchangeRate fixes only accept their
            // required fix-specific field.
            Env env(*this, features);
            Account const carol{"carol"};

            env.fund(XRP(1000), carol);
            env.close();

            // BookExchangeRate fixes require sfBookDirectory.
            auto missingBookDirectory = ledgerStateFix::bookExchangeRate(carol, uint256{1});
            missingBookDirectory.removeMember(sfBookDirectory.jsonName);
            env(missingBookDirectory, Ter(temINVALID));

            // BookExchangeRate fixes reject fields that belong to other
            // LedgerStateFix types.
            auto extraOwner = ledgerStateFix::bookExchangeRate(carol, uint256{1});
            extraOwner[sfOwner.jsonName] = carol.human();
            env(extraOwner, Ter(temINVALID));
        }

        {
            Env env(*this, features);
            auto const setup = PermissionedDEX(env);
            auto const fixFee = drops(env.current()->fees().increment);

            {
                // Preclaim check: the target directory must exist.
                env(ledgerStateFix::bookExchangeRate(setup.carol, uint256{1}),
                    Fee(fixFee),
                    Ter(tecOBJECT_NOT_FOUND));
            }

            {
                // Preclaim check: the target directory must be a book root
                // page. Owner directories are ltDIR_NODE entries, but they do
                // not carry sfExchangeRate.
                auto const ownerDir = keylet::ownerDir(setup.bob.id());
                auto const ownerDirSle = env.le(ownerDir);
                BEAST_EXPECT(ownerDirSle);
                BEAST_EXPECT(!ownerDirSle->isFieldPresent(sfExchangeRate));

                env(ledgerStateFix::bookExchangeRate(setup.carol, ownerDir.key),
                    Fee(fixFee),
                    Ter(tecNO_PERMISSION));
            }

            {
                // Preclaim check: a correct sfExchangeRate leaves nothing to
                // repair.
                auto const bobOfferSeq{env.seq(setup.bob)};
                env(offer(setup.bob, XRP(100), setup.usd(40)));
                env.close();

                auto const sle = env.le(keylet::offer(setup.bob.id(), bobOfferSeq));
                BEAST_EXPECT(sle);

                auto const dirKey = sle->getFieldH256(sfBookDirectory);
                {
                    auto const dirSle = env.le(Keylet(ltDIR_NODE, dirKey));
                    BEAST_EXPECT(dirSle);
                    auto const exchangeRate = dirSle->getFieldU64(sfExchangeRate);
                    auto const quality = getQuality(dirKey);
                    BEAST_EXPECT(exchangeRate == quality);
                }

                env(ledgerStateFix::bookExchangeRate(setup.carol, dirKey),
                    Fee(fixFee),
                    Ter(tecNO_PERMISSION));
            }
        }

        {
            // Repair path: start without fixCleanup3_2_0 to produce the
            // mismatch, then enable the amendment and fix it.
            Env env(*this, features - fixCleanup3_2_0);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            // Bob places a hybrid offer.
            env(offer(bob_, XRP(100), USD(40)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            // Alice partially crosses Bob.
            auto const aliceOfferSeq{env.seq(alice_)};
            env(offer(alice_, USD(100), XRP(300)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            auto const sle = env.le(keylet::offer(alice_.id(), aliceOfferSeq));
            BEAST_EXPECT(sle);

            auto const openDirKey =
                sle->getFieldArray(sfAdditionalBooks)[0].getFieldH256(sfBookDirectory);

            auto const preCrossingQuality = std::uint64_t{5623825668291712342ULL};
            auto const postCrossingQuality = std::uint64_t{5623825668291712341ULL};

            // Confirm mismatch exists.
            {
                auto const dirSle = env.le(Keylet(ltDIR_NODE, openDirKey));
                BEAST_EXPECT(dirSle);
                auto const exchangeRate = dirSle->getFieldU64(sfExchangeRate);
                auto const quality = getQuality(openDirKey);
                BEAST_EXPECT(exchangeRate == preCrossingQuality);
                BEAST_EXPECT(quality == postCrossingQuality);
                BEAST_EXPECT(exchangeRate != quality);
            }

            // Enable fixCleanup3_2_0 and apply the LedgerStateFix.
            env.enableFeature(fixCleanup3_2_0);
            env.close();

            auto const fixFee = drops(env.current()->fees().increment);
            env(ledgerStateFix::bookExchangeRate(carol_, openDirKey), Fee(fixFee));
            env.close();

            // Confirm sfExchangeRate now matches the key quality.
            {
                auto const dirSle = env.le(Keylet(ltDIR_NODE, openDirKey));
                BEAST_EXPECT(dirSle);
                auto const exchangeRate = dirSle->getFieldU64(sfExchangeRate);
                auto const quality = getQuality(openDirKey);
                BEAST_EXPECT(exchangeRate == postCrossingQuality);
                BEAST_EXPECT(quality == postCrossingQuality);
                BEAST_EXPECT(exchangeRate == quality);
            }

            // Submitting again should fail — nothing to fix.
            env(ledgerStateFix::bookExchangeRate(carol_, openDirKey),
                Fee(fixFee),
                Ter(tecNO_PERMISSION));
        }
    }

    void
    testCancelRegularOfferWithDomainCreate(FeatureBitset features)
    {
        bool const fixEnabled = features[fixCleanup3_2_0];

        testcase << "Cancel regular offer via domain OfferCreate"
                 << (fixEnabled ? " (fixCleanup3_2_0 enabled)" : " (fixCleanup3_2_0 disabled)");

        // An OfferCreate with sfDomainID and sfOfferSequence pointing to
        // the user's own non-domain offer should atomically cancel the
        // regular offer and place the new domain offer.
        //
        // Pre-fixCleanup3_2_0: ValidPermissionedDEX flagged the deleted
        // regular offer, so the transaction failed with tecINVARIANT_FAILED.
        // Post-fixCleanup3_2_0: the invariant ignores deletions and the
        // transaction succeeds.

        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        auto const regularSeq = env.seq(bob);
        env(offer(bob, XRP(10), USD(10)));
        env.close();
        BEAST_EXPECT(checkOffer(env, bob, regularSeq, XRP(10), USD(10), 0, false));

        auto const domainSeq = env.seq(bob);
        if (fixEnabled)
        {
            env(offer(bob, XRP(20), USD(20)),
                Domain(domainID),
                Json(jss::OfferSequence, regularSeq));
            env.close();
            BEAST_EXPECT(!offerExists(env, bob, regularSeq));
            BEAST_EXPECT(checkOffer(env, bob, domainSeq, XRP(20), USD(20), 0, true));
        }
        else
        {
            env(offer(bob, XRP(20), USD(20)),
                Domain(domainID),
                Json(jss::OfferSequence, regularSeq),
                Ter(tecINVARIANT_FAILED));
            env.close();
            BEAST_EXPECT(offerExists(env, bob, regularSeq));
            BEAST_EXPECT(!offerExists(env, bob, domainSeq));
        }
    }

public:
    void
    run() override
    {
        FeatureBitset const all{jtx::testableAmendments()};

        // Test domain offer (w/o hybrid)
        testOfferCreate(all);
        testOfferCreate(all - fixCleanup3_2_0);
        testPayment(all);
        testPayment(all - fixCleanup3_2_0);
        testBookStep(all);
        testRippling(all);
        testOfferTokenIssuerInDomain(all);
        testRemoveUnfundedOffer(all);
        testAmmNotUsed(all);
        testAutoBridge(all);

        // Test hybrid offers
        testHybridOfferCreate(all);
        testHybridBookStep(all);
        testHybridInvalidOffer(all - fixCleanup3_3_0);
        testHybridInvalidOffer(all);
        testHybridOpenBookAfterCredentialExpiry(all);
        testHybridOfferDirectories(all);
        testHybridMalformedOffer(all);
        testHybridMalformedOffer(all - fixCleanup3_1_3);
        testHybridOfferCrossingQuality(all);
        testHybridOfferCrossingQuality(all - fixCleanup3_2_0);
        testBookExchangeRateFix(all);

        // Cancelling a regular offer in a domain OfferCreate is allowed
        // only after fixCleanup3_2_0.
        testCancelRegularOfferWithDomainCreate(all);
        testCancelRegularOfferWithDomainCreate(all - fixCleanup3_2_0);
    }
};

BEAST_DEFINE_TESTSUITE(PermissionedDEX, app, xrpl);

}  // namespace xrpl::test
