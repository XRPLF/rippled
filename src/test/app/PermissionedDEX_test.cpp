#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>

#include <xrpld/app/tx/detail/PermissionedDomainSet.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ripple {
namespace test {

using namespace jtx;

class PermissionedDEX_test : public beast::unit_test::suite
{
    [[nodiscard]] bool
    offerExists(Env const& env, Account const& account, std::uint32_t offerSeq)
    {
        return static_cast<bool>(env.le(keylet::offer(account.id(), offerSeq)));
    }

    [[nodiscard]] bool
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
                              std::optional<uint256> domain =
                                  std::nullopt) -> bool {
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

            auto const& additionalBookDirs =
                sle->getFieldArray(sfAdditionalBooks);

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

    uint256
    getBookDirKey(
        Book const& book,
        STAmount const& takerPays,
        STAmount const& takerGets)
    {
        return keylet::quality(
                   keylet::book(book), getRate(takerGets, takerPays))
            .key;
    }

    std::optional<uint256>
    getDefaultOfferDirKey(
        Env const& env,
        Account const& account,
        std::uint32_t offerSeq)
    {
        if (auto const sle = env.le(keylet::offer(account.id(), offerSeq)))
            return Keylet(ltDIR_NODE, (*sle)[sfBookDirectory]).key;

        return {};
    }

    [[nodiscard]] bool
    checkDirectorySize(Env const& env, uint256 directory, std::uint32_t dirSize)
    {
        std::optional<std::uint64_t> pageIndex{0};
        std::uint32_t dirCnt = 0;

        do
        {
            auto const page = env.le(keylet::page(directory, *pageIndex));
            if (!page)
                break;

            pageIndex = (*page)[~sfIndexNext];
            dirCnt += (*page)[sfIndexes].size();

        } while (pageIndex.value_or(0));

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

            env(offer(bob, XRP(10), USD(10)),
                domain(domainID),
                ter(temDISABLED));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();
        }

        // preclaim - someone outside of the domain cannot create domain offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            env(offer(devin, XRP(10), USD(10)),
                domain(domainID),
                ter(tecNO_PERMISSION));
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            // devin still cannot create offer since he didn't accept credential
            env(offer(devin, XRP(10), USD(10)),
                domain(domainID),
                ter(tecNO_PERMISSION));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            env(offer(devin, XRP(10), USD(10)), domain(domainID));
            env.close();
        }

        // preclaim - someone with expired cred cannot create domain offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            auto jv = credentials::create(devin, domainOwner, credType);
            uint32_t const t = env.current()
                                   ->info()
                                   .parentCloseTime.time_since_epoch()
                                   .count();
            jv[sfExpiration.jsonName] = t + 20;
            env(jv);

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can still create offer while his cred is not expired
            env(offer(devin, XRP(10), USD(10)), domain(domainID));
            env.close();

            // time advance
            env.close(std::chrono::seconds(20));

            // devin cannot create offer with expired cred
            env(offer(devin, XRP(10), USD(10)),
                domain(domainID),
                ter(tecNO_PERMISSION));
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

            env(offer(bob, XRP(10), USD(10)),
                domain(badDomain),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // apply - offer can be created even if takergets issuer is not in
        // domain
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(credentials::deleteCred(
                domainOwner, gw, domainOwner, credType));
            env.close();

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));
        }

        // apply - offer can be created even if takerpays issuer is not in
        // domain
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(credentials::deleteCred(
                domainOwner, gw, domainOwner, credType));
            env.close();

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), XRP(10)), domain(domainID));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, bobOfferSeq, USD(10), XRP(10), 0, true));
        }

        // apply - two domain offers cross with each other
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            // a non domain offer cannot cross with domain offer
            env(offer(carol, USD(10), XRP(10)));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));

            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)), domain(domainID));
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

                env(offer(bob, XRP(10), USD(10)), domain(domainID));
                env.close();
                BEAST_EXPECT(checkOffer(
                    env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));
            }

            for (auto const offerSeq : offerSeqs)
            {
                env(offer_cancel(bob, offerSeq));
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
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID),
                ter(temDISABLED));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();

            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            env(pay(bob, alice, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID));
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
                path(~USD),
                sendmax(XRP(10)),
                domain(badDomain),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // preclaim - payment with non-domain destination fails
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            // create devin account who is not part of the domain
            Account devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            // devin is not part of domain
            env(pay(alice, devin, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID),
                ter(tecNO_PERMISSION));
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            // devin has not yet accepted cred
            env(pay(alice, devin, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID),
                ter(tecNO_PERMISSION));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can now receive payment after he is in domain
            env(pay(alice, devin, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID));
            env.close();
        }

        // preclaim - non-domain sender cannot send payment
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            // create devin account who is not part of the domain
            Account devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            // devin tries to send domain payment
            env(pay(devin, alice, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID),
                ter(tecNO_PERMISSION));
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            // devin has not yet accepted cred
            env(pay(devin, alice, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID),
                ter(tecNO_PERMISSION));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can now send payment after he is in domain
            env(pay(devin, alice, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID));
            env.close();
        }

        // apply - domain owner can always send and receive domain payment
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            // domain owner can always be destination
            env(pay(alice, domainOwner, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID));
            env.close();

            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            // domain owner can send
            env(pay(domainOwner, alice, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID));
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
            BEAST_EXPECT(
                checkOffer(env, bob, regularOfferSeq, XRP(10), USD(10)));

            auto const regularDirKey =
                getDefaultOfferDirKey(env, bob, regularOfferSeq);
            BEAST_EXPECT(regularDirKey);
            BEAST_EXPECT(checkDirectorySize(env, *regularDirKey, 1));

            // a domain payment cannot consume regular offers
            env(pay(alice, carol, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID),
                ter(tecPATH_PARTIAL));
            env.close();

            // create a domain offer
            auto const domainOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(
                env, bob, domainOfferSeq, XRP(10), USD(10), 0, true));

            auto const domainDirKey =
                getDefaultOfferDirKey(env, bob, domainOfferSeq);
            BEAST_EXPECT(domainDirKey);
            BEAST_EXPECT(checkDirectorySize(env, *domainDirKey, 1));

            // cross-currency permissioned payment consumed
            // domain offer instead of regular offer
            env(pay(alice, carol, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID));
            env.close();
            BEAST_EXPECT(!offerExists(env, bob, domainOfferSeq));
            BEAST_EXPECT(
                checkOffer(env, bob, regularOfferSeq, XRP(10), USD(10)));

            // domain directory is empty
            BEAST_EXPECT(checkDirectorySize(env, *domainDirKey, 0));
            BEAST_EXPECT(checkDirectorySize(env, *regularDirKey, 1));
        }

        // test domain payment consuming two offers in the path
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const EUR = gw["EUR"];
            env.trust(EUR(1000), alice);
            env.close();
            env.trust(EUR(1000), bob);
            env.close();
            env.trust(EUR(1000), carol);
            env.close();
            env(pay(gw, bob, EUR(100)));
            env.close();

            // create XRP/USD domain offer
            auto const usdOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, true));

            // payment fail because there isn't eur offer
            env(pay(alice, carol, EUR(10)),
                path(~USD, ~EUR),
                sendmax(XRP(10)),
                domain(domainID),
                ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, true));

            // bob creates a regular USD/EUR offer
            auto const regularOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), EUR(10)));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, bob, regularOfferSeq, USD(10), EUR(10)));

            // alice tries to pay again, but still fails because the regular
            // offer cannot be consumed
            env(pay(alice, carol, EUR(10)),
                path(~USD, ~EUR),
                sendmax(XRP(10)),
                domain(domainID),
                ter(tecPATH_PARTIAL));
            env.close();

            // bob creates a domain USD/EUR offer
            auto const eurOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), EUR(10)), domain(domainID));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, bob, eurOfferSeq, USD(10), EUR(10), 0, true));

            // alice successfully consume two domain offers: xrp/usd and usd/eur
            env(pay(alice, carol, EUR(5)),
                sendmax(XRP(5)),
                domain(domainID),
                path(~USD, ~EUR));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, usdOfferSeq, XRP(5), USD(5), 0, true));
            BEAST_EXPECT(
                checkOffer(env, bob, eurOfferSeq, USD(5), EUR(5), 0, true));

            // alice successfully consume two domain offers and deletes them
            // we compute path this time using `paths`
            env(pay(alice, carol, EUR(5)),
                sendmax(XRP(5)),
                domain(domainID),
                paths(XRP));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob, usdOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob, eurOfferSeq));

            // regular offer is not consumed
            BEAST_EXPECT(
                checkOffer(env, bob, regularOfferSeq, USD(10), EUR(10)));
        }

        // domain payment cannot consume offer from another domain
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // Fund devin and create USD trustline
            Account badDomainOwner("badDomainOwner");
            Account devin("devin");
            env.fund(XRP(1000), badDomainOwner, devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            auto const badCredType = "badCred";
            pdomain::Credentials credentials{{badDomainOwner, badCredType}};
            env(pdomain::setTx(badDomainOwner, credentials));

            auto objects = pdomain::getObjects(badDomainOwner, env);
            auto const badDomainID = objects.begin()->first;

            env(credentials::create(devin, badDomainOwner, badCredType));
            env.close();
            env(credentials::accept(devin, badDomainOwner, badCredType));

            // devin creates a domain offer in another domain
            env(offer(devin, XRP(10), USD(10)), domain(badDomainID));
            env.close();

            // domain payment can't consume an offer from another domain
            env(pay(alice, carol, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID),
                ter(tecPATH_PARTIAL));
            env.close();

            // bob creates an offer under the right domain
            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));

            // domain payment now consumes from the right domain
            env(pay(alice, carol, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                domain(domainID));
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

            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            // fund devin but don't create a USD trustline with gateway
            Account devin("devin");
            env.fund(XRP(1000), devin);
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // successful payment because offer is consumed
            env(pay(devin, alice, USD(10)), sendmax(XRP(10)), domain(domainID));
            env.close();
        }

        // offer becomes unfunded when offer owner's cred expires
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw, devin, USD(100)));
            env.close();

            auto jv = credentials::create(devin, domainOwner, credType);
            uint32_t const t = env.current()
                                   ->info()
                                   .parentCloseTime.time_since_epoch()
                                   .count();
            jv[sfExpiration.jsonName] = t + 20;
            env(jv);

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can still create offer while his cred is not expired
            auto const offerSeq{env.seq(devin)};
            env(offer(devin, XRP(10), USD(10)), domain(domainID));
            env.close();

            // devin's offer can still be consumed while his cred isn't expired
            env(pay(alice, carol, USD(5)),
                path(~USD),
                sendmax(XRP(5)),
                domain(domainID));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, devin, offerSeq, XRP(5), USD(5), 0, true));

            // advance time
            env.close(std::chrono::seconds(20));

            // devin's offer is unfunded now due to expired cred
            env(pay(alice, carol, USD(5)),
                path(~USD),
                sendmax(XRP(5)),
                domain(domainID),
                ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, devin, offerSeq, XRP(5), USD(5), 0, true));
        }

        // offer becomes unfunded when offer owner's cred is removed
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const offerSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            // bob's offer can still be consumed while his cred exists
            env(pay(alice, carol, USD(5)),
                path(~USD),
                sendmax(XRP(5)),
                domain(domainID));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, bob, offerSeq, XRP(5), USD(5), 0, true));

            // remove bob's cred
            env(credentials::deleteCred(
                domainOwner, bob, domainOwner, credType));
            env.close();

            // bob's offer is unfunded now due to expired cred
            env(pay(alice, carol, USD(5)),
                path(~USD),
                sendmax(XRP(5)),
                domain(domainID),
                ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, bob, offerSeq, XRP(5), USD(5), 0, true));
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

        auto const EURA = alice["EUR"];
        auto const EURB = bob["EUR"];

        env.trust(EURA(100), bob);
        env.trust(EURB(100), carol);
        env.close();

        // remove bob from domain
        env(credentials::deleteCred(domainOwner, bob, domainOwner, credType));
        env.close();

        // alice can still ripple through bob even though he's not part
        // of the domain, this is intentional
        env(pay(alice, carol, EURB(10)), paths(EURA), domain(domainID));
        env.close();
        env.require(balance(bob, EURA(10)), balance(carol, EURB(10)));

        // carol sets no ripple on bob
        env(trust(carol, bob["EUR"](0), bob, tfSetNoRipple));
        env.close();

        // payment no longer works because carol has no ripple on bob
        env(pay(alice, carol, EURB(5)),
            paths(EURA),
            domain(domainID),
            ter(tecPATH_DRY));
        env.close();
        env.require(balance(bob, EURA(10)), balance(carol, EURB(10)));
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
        env(offer(bob, XRP(10), USD(10)), domain(domainID));
        env.close();

        // create an usd/xrp offer with usd as takerpays
        auto const bobOffer2Seq{env.seq(bob)};
        env(offer(bob, USD(10), XRP(10)), domain(domainID), txflags(tfPassive));
        env.close();

        BEAST_EXPECT(
            checkOffer(env, bob, bobOffer1Seq, XRP(10), USD(10), 0, true));
        BEAST_EXPECT(checkOffer(
            env, bob, bobOffer2Seq, USD(10), XRP(10), lsfPassive, true));

        // remove gateway from domain
        env(credentials::deleteCred(domainOwner, gw, domainOwner, credType));
        env.close();

        // payment succeeds even if issuer is not in domain
        // xrp/usd offer is consumed
        env(pay(alice, carol, USD(10)),
            path(~USD),
            sendmax(XRP(10)),
            domain(domainID));
        env.close();
        BEAST_EXPECT(!offerExists(env, bob, bobOffer1Seq));

        // payment succeeds even if issuer is not in domain
        // usd/xrp offer is consumed
        env(pay(alice, carol, XRP(10)),
            path(~XRP),
            sendmax(USD(10)),
            domain(domainID));
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
        env(offer(alice, XRP(100), USD(100)), domain(domainID));
        env.close();

        auto const bobOfferSeq{env.seq(bob)};
        env(offer(bob, XRP(20), USD(20)), domain(domainID));
        env.close();

        BEAST_EXPECT(
            checkOffer(env, bob, bobOfferSeq, XRP(20), USD(20), 0, true));
        BEAST_EXPECT(
            checkOffer(env, alice, aliceOfferSeq, XRP(100), USD(100), 0, true));

        auto const domainDirKey = getDefaultOfferDirKey(env, bob, bobOfferSeq);
        BEAST_EXPECT(domainDirKey);
        BEAST_EXPECT(checkDirectorySize(env, *domainDirKey, 2));

        // remove alice from domain and thus alice's offer becomes unfunded
        env(credentials::deleteCred(domainOwner, alice, domainOwner, credType));
        env.close();

        env(pay(gw, carol, USD(10)),
            path(~USD),
            sendmax(XRP(10)),
            domain(domainID));
        env.close();

        BEAST_EXPECT(
            checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));

        // alice's unfunded offer is removed implicitly
        BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
        BEAST_EXPECT(checkDirectorySize(env, *domainDirKey, 1));
    }

    void
    testAmmNotUsed(FeatureBitset features)
    {
        testcase("AMM not used");

        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);
        AMM amm(env, alice, XRP(10), USD(50));

        // a domain payment isn't able to consume AMM
        env(pay(bob, carol, USD(5)),
            path(~USD),
            sendmax(XRP(5)),
            domain(domainID),
            ter(tecPATH_PARTIAL));
        env.close();

        // a non domain payment can use AMM
        env(pay(bob, carol, USD(5)), path(~USD), sendmax(XRP(5)));
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
                domain(domainID),
                txflags(tfHybrid),
                ter(temDISABLED));
            env.close();

            env(offer(bob, XRP(10), USD(10)),
                txflags(tfHybrid),
                ter(temINVALID_FLAG));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();

            // hybrid offer must have domainID
            env(offer(bob, XRP(10), USD(10)),
                txflags(tfHybrid),
                ter(temINVALID_FLAG));
            env.close();

            // hybrid offer must have domainID
            auto const offerSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)),
                txflags(tfHybrid),
                domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(
                env, bob, offerSeq, XRP(10), USD(10), lsfHybrid, true));
        }

        // apply - domain offer can cross with hybrid
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)),
                txflags(tfHybrid),
                domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(
                env, bob, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));
            BEAST_EXPECT(offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)), domain(domainID));
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
            env(offer(bob, XRP(10), USD(10)),
                txflags(tfHybrid),
                domain(domainID));
            env.close();

            BEAST_EXPECT(offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, bob) == 3);
            BEAST_EXPECT(checkOffer(
                env, bob, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));

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
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, true));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            // hybrid offer auto crosses with domain offer
            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)),
                domain(domainID),
                txflags(tfHybrid));
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

            BEAST_EXPECT(
                checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, false));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            // hybrid offer auto crosses with domain offer
            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)),
                domain(domainID),
                txflags(tfHybrid));
            env.close();

            BEAST_EXPECT(offerExists(env, alice, aliceOfferSeq));
            BEAST_EXPECT(offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(
                checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10), 0, false));
            BEAST_EXPECT(checkOffer(
                env, alice, aliceOfferSeq, USD(10), XRP(10), lsfHybrid, true));
            BEAST_EXPECT(ownerCount(env, alice) == 3);
        }
    }

    void
    testHybridInvalidOffer(FeatureBitset features)
    {
        testcase("Hybrid invalid offer");

        // bob has a hybrid offer and then he is removed from domain.
        // in this case, the hybrid offer will be considered as unfunded even in
        // a regular payment
        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        auto const hybridOfferSeq{env.seq(bob)};
        env(offer(bob, XRP(50), USD(50)), txflags(tfHybrid), domain(domainID));
        env.close();

        // remove bob from domain
        env(credentials::deleteCred(domainOwner, bob, domainOwner, credType));
        env.close();

        // bob's hybrid offer is unfunded and can not be consumed in a domain
        // payment
        env(pay(alice, carol, USD(5)),
            path(~USD),
            sendmax(XRP(5)),
            domain(domainID),
            ter(tecPATH_PARTIAL));
        env.close();
        BEAST_EXPECT(checkOffer(
            env, bob, hybridOfferSeq, XRP(50), USD(50), lsfHybrid, true));

        // bob's unfunded hybrid offer can't be consumed even with a regular
        // payment
        env(pay(alice, carol, USD(5)),
            path(~USD),
            sendmax(XRP(5)),
            ter(tecPATH_PARTIAL));
        env.close();
        BEAST_EXPECT(checkOffer(
            env, bob, hybridOfferSeq, XRP(50), USD(50), lsfHybrid, true));

        // create a regular offer
        auto const regularOfferSeq{env.seq(bob)};
        env(offer(bob, XRP(10), USD(10)));
        env.close();
        BEAST_EXPECT(offerExists(env, bob, regularOfferSeq));
        BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, XRP(10), USD(10)));

        auto const sleHybridOffer =
            env.le(keylet::offer(bob.id(), hybridOfferSeq));
        BEAST_EXPECT(sleHybridOffer);
        auto const openDir =
            sleHybridOffer->getFieldArray(sfAdditionalBooks)[0].getFieldH256(
                sfBookDirectory);
        BEAST_EXPECT(checkDirectorySize(env, openDir, 2));

        // this normal payment should consume the regular offer and remove the
        // unfunded hybrid offer
        env(pay(alice, carol, USD(5)), path(~USD), sendmax(XRP(5)));
        env.close();

        BEAST_EXPECT(!offerExists(env, bob, hybridOfferSeq));
        BEAST_EXPECT(checkOffer(env, bob, regularOfferSeq, XRP(5), USD(5)));
        BEAST_EXPECT(checkDirectorySize(env, openDir, 1));
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
            env(offer(bob, XRP(10), USD(10)),
                txflags(tfHybrid),
                domain(domainID));
            env.close();

            env(pay(alice, carol, USD(5)),
                path(~USD),
                sendmax(XRP(5)),
                domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(
                env, bob, hybridOfferSeq, XRP(5), USD(5), lsfHybrid, true));

            // hybrid offer can't be consumed since bob is not in domain anymore
            env(pay(alice, carol, USD(5)), path(~USD), sendmax(XRP(5)));
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
            Account badDomainOwner("badDomainOwner");
            Account devin("devin");
            env.fund(XRP(1000), badDomainOwner, devin);
            env.close();

            auto const badCredType = "badCred";
            pdomain::Credentials credentials{{badDomainOwner, badCredType}};
            env(pdomain::setTx(badDomainOwner, credentials));

            auto objects = pdomain::getObjects(badDomainOwner, env);
            auto const badDomainID = objects.begin()->first;

            env(credentials::create(devin, badDomainOwner, badCredType));
            env.close();
            env(credentials::accept(devin, badDomainOwner, badCredType));
            env.close();

            auto const hybridOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)),
                txflags(tfHybrid),
                domain(domainID));
            env.close();

            // other domains can't consume the offer
            env(pay(devin, badDomainOwner, USD(5)),
                path(~USD),
                sendmax(XRP(5)),
                domain(badDomainID),
                ter(tecPATH_DRY));
            env.close();
            BEAST_EXPECT(checkOffer(
                env, bob, hybridOfferSeq, XRP(10), USD(10), lsfHybrid, true));

            env(pay(alice, carol, USD(5)),
                path(~USD),
                sendmax(XRP(5)),
                domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(
                env, bob, hybridOfferSeq, XRP(5), USD(5), lsfHybrid, true));

            // hybrid offer can't be consumed since bob is not in domain anymore
            env(pay(alice, carol, USD(5)), path(~USD), sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob, hybridOfferSeq));
        }

        // test domain payment consuming two offers w/ hybrid offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const EUR = gw["EUR"];
            env.trust(EUR(1000), alice);
            env.close();
            env.trust(EUR(1000), bob);
            env.close();
            env.trust(EUR(1000), carol);
            env.close();
            env(pay(gw, bob, EUR(100)));
            env.close();

            auto const usdOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)), domain(domainID));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, true));

            // payment fail because there isn't eur offer
            env(pay(alice, carol, EUR(5)),
                path(~USD, ~EUR),
                sendmax(XRP(5)),
                domain(domainID),
                ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(
                checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, true));

            // bob creates a hybrid eur offer
            auto const eurOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), EUR(10)),
                domain(domainID),
                txflags(tfHybrid));
            env.close();
            BEAST_EXPECT(checkOffer(
                env, bob, eurOfferSeq, USD(10), EUR(10), lsfHybrid, true));

            // alice successfully consume two domain offers: xrp/usd and usd/eur
            env(pay(alice, carol, EUR(5)),
                path(~USD, ~EUR),
                sendmax(XRP(5)),
                domain(domainID));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, usdOfferSeq, XRP(5), USD(5), 0, true));
            BEAST_EXPECT(checkOffer(
                env, bob, eurOfferSeq, USD(5), EUR(5), lsfHybrid, true));
        }

        // test regular payment using a regular offer and a hybrid offer
        {
            Env env(*this, features);
            auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const EUR = gw["EUR"];
            env.trust(EUR(1000), alice);
            env.close();
            env.trust(EUR(1000), bob);
            env.close();
            env.trust(EUR(1000), carol);
            env.close();
            env(pay(gw, bob, EUR(100)));
            env.close();

            // bob creates a regular usd offer
            auto const usdOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, usdOfferSeq, XRP(10), USD(10), 0, false));

            // bob creates a hybrid eur offer
            auto const eurOfferSeq{env.seq(bob)};
            env(offer(bob, USD(10), EUR(10)),
                domain(domainID),
                txflags(tfHybrid));
            env.close();
            BEAST_EXPECT(checkOffer(
                env, bob, eurOfferSeq, USD(10), EUR(10), lsfHybrid, true));

            // alice successfully consume two offers: xrp/usd and usd/eur
            env(pay(alice, carol, EUR(5)), path(~USD, ~EUR), sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(
                checkOffer(env, bob, usdOfferSeq, XRP(5), USD(5), 0, false));
            BEAST_EXPECT(checkOffer(
                env, bob, eurOfferSeq, USD(5), EUR(5), lsfHybrid, true));
        }
    }

    void
    testHybridOfferDirectories(FeatureBitset features)
    {
        Env env(*this, features);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            PermissionedDEX(env);

        std::vector<std::uint32_t> offerSeqs;
        offerSeqs.reserve(100);

        Book domainBook{Issue(XRP), Issue(USD), domainID};
        Book openBook{Issue(XRP), Issue(USD), std::nullopt};

        auto const domainDir = getBookDirKey(domainBook, XRP(10), USD(10));
        auto const openDir = getBookDirKey(openBook, XRP(10), USD(10));

        size_t dirCnt = 100;

        for (size_t i = 1; i <= dirCnt; i++)
        {
            auto const bobOfferSeq{env.seq(bob)};
            offerSeqs.emplace_back(bobOfferSeq);
            env(offer(bob, XRP(10), USD(10)),
                txflags(tfHybrid),
                domain(domainID));
            env.close();

            auto const sleOffer = env.le(keylet::offer(bob.id(), bobOfferSeq));
            BEAST_EXPECT(sleOffer);
            BEAST_EXPECT(sleOffer->getFieldH256(sfBookDirectory) == domainDir);
            BEAST_EXPECT(
                sleOffer->getFieldArray(sfAdditionalBooks).size() == 1);
            BEAST_EXPECT(
                sleOffer->getFieldArray(sfAdditionalBooks)[0].getFieldH256(
                    sfBookDirectory) == openDir);

            BEAST_EXPECT(checkOffer(
                env, bob, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));
            BEAST_EXPECT(checkDirectorySize(env, domainDir, i));
            BEAST_EXPECT(checkDirectorySize(env, openDir, i));
        }

        for (auto const offerSeq : offerSeqs)
        {
            env(offer_cancel(bob, offerSeq));
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
        auto const EUR = gw["EUR"];

        for (auto const& account : {alice, bob, carol})
        {
            env(trust(account, EUR(10000)));
            env.close();
        }

        env(pay(gw, carol, EUR(1)));
        env.close();

        auto const aliceOfferSeq{env.seq(alice)};
        auto const bobOfferSeq{env.seq(bob)};
        env(offer(alice, XRP(100), USD(1)), domain(domainID));
        env(offer(bob, EUR(1), XRP(100)), domain(domainID));
        env.close();

        // carol's offer should cross bob and alice's offers due to auto
        // bridging
        auto const carolOfferSeq{env.seq(carol)};
        env(offer(carol, USD(1), EUR(1)), domain(domainID));
        env.close();

        BEAST_EXPECT(!offerExists(env, bob, aliceOfferSeq));
        BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
        BEAST_EXPECT(!offerExists(env, bob, carolOfferSeq));
    }

public:
    void
    run() override
    {
        FeatureBitset const all{jtx::testable_amendments()};

        // Test domain offer (w/o hyrbid)
        testOfferCreate(all);
        testPayment(all);
        testBookStep(all);
        testRippling(all);
        testOfferTokenIssuerInDomain(all);
        testRemoveUnfundedOffer(all);
        testAmmNotUsed(all);
        testAutoBridge(all);

        // Test hybrid offers
        testHybridOfferCreate(all);
        testHybridBookStep(all);
        testHybridInvalidOffer(all);
        testHybridOfferDirectories(all);
    }
};

BEAST_DEFINE_TESTSUITE(PermissionedDEX, app, ripple);

}  // namespace test
}  // namespace ripple
