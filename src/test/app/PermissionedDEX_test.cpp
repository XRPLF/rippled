#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/credentials.h>
#include <test/jtx/domain.h>
#include <test/jtx/jtx_json.h>
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
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob_, XRP(10), USD(10)), Domain(domainID), Ter(temDISABLED));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();
        }

        // preclaim - someone outside of the domain cannot create domain offer
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw_, devin, USD(100)));
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
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw_, devin, USD(100)));
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
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);
            uint256 const badDomain{
                "F10D0CC9A0F9A3CBF585B80BE09A186483668FDBDD39AA7E3370F3649CE134"
                "E5"};

            env(offer(bob_, XRP(10), USD(10)), Domain(badDomain), Ter(tecNO_PERMISSION));
            env.close();
        }

        // apply - offer can be created even if takergets issuer is not in
        // domain
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(credentials::deleteCred(domainOwner, gw_, domainOwner, credType));
            env.close();

            auto const bobOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, true));
        }

        // apply - offer can be created even if takerpays issuer is not in
        // domain
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(credentials::deleteCred(domainOwner, gw_, domainOwner, credType));
            env.close();

            auto const bobOfferSeq{env.seq(bob_)};
            env(offer(bob_, USD(10), XRP(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, USD(10), XRP(10), 0, true));
        }

        // apply - two domain offers cross with each other
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, true));
            BEAST_EXPECT(ownerCount(env, bob_) == 3);

            // a non domain offer cannot cross with domain offer
            env(offer(carol_, USD(10), XRP(10)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, true));

            auto const aliceOfferSeq{env.seq(alice_)};
            env(offer(alice_, USD(10), XRP(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(!offerExists(env, alice_, aliceOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob_, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, alice_) == 2);
        }

        // apply - create lots of domain offers
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            std::vector<std::uint32_t> offerSeqs;
            offerSeqs.reserve(100);

            for (size_t i = 0; i <= 100; i++)
            {
                auto const bobOfferSeq{env.seq(bob_)};
                offerSeqs.emplace_back(bobOfferSeq);

                env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
                env.close();
                BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, true));
            }

            for (auto const offerSeq : offerSeqs)
            {
                env(offerCancel(bob_, offerSeq));
                env.close();
                BEAST_EXPECT(!offerExists(env, bob_, offerSeq));
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
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(pay(bob_, alice_, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(temDISABLED));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();

            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            env(pay(bob_, alice_, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }

        // preclaim - cannot send payment with non existent domain
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);
            uint256 const badDomain{
                "F10D0CC9A0F9A3CBF585B80BE09A186483668FDBDD39AA7E3370F3649CE134"
                "E5"};

            env(pay(bob_, alice_, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(badDomain),
                Ter(tecNO_PERMISSION));
            env.close();
        }

        // preclaim - payment with non-domain destination fails
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw_, devin, USD(100)));
            env.close();

            // devin is not part of domain
            env(pay(alice_, devin, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecNO_PERMISSION));
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            // devin has not yet accepted cred
            env(pay(alice_, devin, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecNO_PERMISSION));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can now receive payment after he is in domain
            env(pay(alice_, devin, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }

        // preclaim - non-domain sender cannot send payment
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw_, devin, USD(100)));
            env.close();

            // devin tries to send domain payment
            env(pay(devin, alice_, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecNO_PERMISSION));
            env.close();

            // domain owner also issues a credential for devin
            env(credentials::create(devin, domainOwner, credType));
            env.close();

            // devin has not yet accepted cred
            env(pay(devin, alice_, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecNO_PERMISSION));
            env.close();

            env(credentials::accept(devin, domainOwner, credType));
            env.close();

            // devin can now send payment after he is in domain
            env(pay(devin, alice_, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }

        // apply - domain owner can always send and receive domain payment
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // domain owner can always be destination
            env(pay(alice_, domainOwner, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();

            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // domain owner can send
            env(pay(domainOwner, alice_, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
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
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            // create a regular offer without domain
            auto const regularOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, regularOfferSeq, XRP(10), USD(10)));

            auto const regularDirKey = getDefaultOfferDirKey(env, bob_, regularOfferSeq);
            BEAST_EXPECT(regularDirKey);
            BEAST_EXPECT(checkDirectorySize(
                env, *regularDirKey, 1));  // NOLINT(bugprone-unchecked-optional-access)

            // a domain payment cannot consume regular offers
            env(pay(alice_, carol_, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();

            // create a domain offer
            auto const domainOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, domainOfferSeq, XRP(10), USD(10), 0, true));

            auto const domainDirKey = getDefaultOfferDirKey(env, bob_, domainOfferSeq);
            BEAST_EXPECT(domainDirKey);
            BEAST_EXPECT(checkDirectorySize(
                env, *domainDirKey, 1));  // NOLINT(bugprone-unchecked-optional-access)

            // cross-currency permissioned payment consumed
            // domain offer instead of regular offer
            env(pay(alice_, carol_, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();
            BEAST_EXPECT(!offerExists(env, bob_, domainOfferSeq));
            BEAST_EXPECT(checkOffer(env, bob_, regularOfferSeq, XRP(10), USD(10)));

            // domain directory is empty
            BEAST_EXPECT(checkDirectorySize(
                env, *domainDirKey, 0));  // NOLINT(bugprone-unchecked-optional-access)
            BEAST_EXPECT(checkDirectorySize(
                env, *regularDirKey, 1));  // NOLINT(bugprone-unchecked-optional-access)
        }

        // test domain payment consuming two offers in the path
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const eur = gw_["EUR"];
            env.trust(eur(1000), alice_);
            env.close();
            env.trust(eur(1000), bob_);
            env.close();
            env.trust(eur(1000), carol_);
            env.close();
            env(pay(gw_, bob_, eur(100)));
            env.close();

            // create XRP/USD domain offer
            auto const usdOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, usdOfferSeq, XRP(10), USD(10), 0, true));

            // payment fail because there isn't eur offer
            env(pay(alice_, carol_, eur(10)),
                Path(~USD, ~eur),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, usdOfferSeq, XRP(10), USD(10), 0, true));

            // bob_ creates a regular USD/EUR offer
            auto const regularOfferSeq{env.seq(bob_)};
            env(offer(bob_, USD(10), eur(10)));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, regularOfferSeq, USD(10), eur(10)));

            // alice_ tries to pay again, but still fails because the regular
            // offer cannot be consumed
            env(pay(alice_, carol_, eur(10)),
                Path(~USD, ~eur),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();

            // bob_ creates a domain USD/EUR offer
            auto const eurOfferSeq{env.seq(bob_)};
            env(offer(bob_, USD(10), eur(10)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, eurOfferSeq, USD(10), eur(10), 0, true));

            // alice_ successfully consume two domain offers: xrp/usd and usd/eur
            env(pay(alice_, carol_, eur(5)), Sendmax(XRP(5)), Domain(domainID), Path(~USD, ~eur));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, usdOfferSeq, XRP(5), USD(5), 0, true));
            BEAST_EXPECT(checkOffer(env, bob_, eurOfferSeq, USD(5), eur(5), 0, true));

            // alice_ successfully consume two domain offers and deletes them
            // we compute path this time using `paths`
            env(pay(alice_, carol_, eur(5)), Sendmax(XRP(5)), Domain(domainID), Paths(XRP));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob_, usdOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob_, eurOfferSeq));

            // regular offer is not consumed
            BEAST_EXPECT(checkOffer(env, bob_, regularOfferSeq, USD(10), eur(10)));
        }

        // domain payment cannot consume offer from another domain
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            // Fund devin and create USD trustline
            Account const badDomainOwner("badDomainOwner");
            Account const devin("devin");
            env.fund(XRP(1000), badDomainOwner, devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw_, devin, USD(100)));
            env.close();

            auto const badCredType = "badCred";
            pdomain::Credentials const credentials{{badDomainOwner, badCredType}};
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
            env(pay(alice_, carol_, USD(10)),
                Path(~USD),
                Sendmax(XRP(10)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();

            // bob_ creates an offer under the right domain
            auto const bobOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, true));

            // domain payment now consumes from the right domain
            env(pay(alice_, carol_, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob_, bobOfferSeq));
        }

        // sanity check: devin, who is part of the domain but doesn't have a
        // trustline with USD issuer, can successfully make a payment using
        // offer
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
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
            env(pay(devin, alice_, USD(10)), Sendmax(XRP(10)), Domain(domainID));
            env.close();
        }

        // offer becomes unfunded when offer owner's cred expires
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            // create devin account who is not part of the domain
            Account const devin("devin");
            env.fund(XRP(1000), devin);
            env.close();
            env.trust(USD(1000), devin);
            env.close();
            env(pay(gw_, devin, USD(100)));
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
            env(pay(alice_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, devin, offerSeq, XRP(5), USD(5), 0, true));

            // advance time
            env.close(std::chrono::seconds(20));

            // devin's offer is unfunded now due to expired cred
            env(pay(alice_, carol_, USD(5)),
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
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const offerSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            // bob_'s offer can still be consumed while his cred exists
            env(pay(alice_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, offerSeq, XRP(5), USD(5), 0, true));

            // remove bob_'s cred
            env(credentials::deleteCred(domainOwner, bob_, domainOwner, credType));
            env.close();

            // bob_'s offer is unfunded now due to expired cred
            env(pay(alice_, carol_, USD(5)),
                Path(~USD),
                Sendmax(XRP(5)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, offerSeq, XRP(5), USD(5), 0, true));
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
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
            PermissionedDEX(env);

        auto const eura = alice_["EUR"];
        auto const eurb = bob_["EUR"];

        env.trust(eura(100), bob_);
        env.trust(eurb(100), carol_);
        env.close();

        // remove bob_ from domain
        env(credentials::deleteCred(domainOwner, bob_, domainOwner, credType));
        env.close();

        // alice_ can still ripple through bob_ even though he's not part
        // of the domain, this is intentional
        env(pay(alice_, carol_, eurb(10)), Paths(eura), Domain(domainID));
        env.close();
        env.require(Balance(bob_, eura(10)), Balance(carol_, eurb(10)));

        // carol_ sets no ripple on bob_
        env(trust(carol_, bob_["EUR"](0), bob_, tfSetNoRipple));
        env.close();

        // payment no longer works because carol_ has no ripple on bob_
        env(pay(alice_, carol_, eurb(5)), Paths(eura), Domain(domainID), Ter(tecPATH_DRY));
        env.close();
        env.require(Balance(bob_, eura(10)), Balance(carol_, eurb(10)));
    }

    void
    testOfferTokenIssuerInDomain(FeatureBitset features)
    {
        testcase("Offer token issuer in domain");

        // whether the issuer is in the domain should NOT affect whether an
        // offer can be consumed in domain payment
        Env env(*this, features);
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
            PermissionedDEX(env);

        // create an xrp/usd offer with usd as takergets
        auto const bobOffer1Seq{env.seq(bob_)};
        env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
        env.close();

        // create an usd/xrp offer with usd as takerpays
        auto const bobOffer2Seq{env.seq(bob_)};
        env(offer(bob_, USD(10), XRP(10)), Domain(domainID), Txflags(tfPassive));
        env.close();

        BEAST_EXPECT(checkOffer(env, bob_, bobOffer1Seq, XRP(10), USD(10), 0, true));
        BEAST_EXPECT(checkOffer(env, bob_, bobOffer2Seq, USD(10), XRP(10), lsfPassive, true));

        // remove gateway from domain
        env(credentials::deleteCred(domainOwner, gw_, domainOwner, credType));
        env.close();

        // payment succeeds even if issuer is not in domain
        // xrp/usd offer is consumed
        env(pay(alice_, carol_, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
        env.close();
        BEAST_EXPECT(!offerExists(env, bob_, bobOffer1Seq));

        // payment succeeds even if issuer is not in domain
        // usd/xrp offer is consumed
        env(pay(alice_, carol_, XRP(10)), Path(~XRP), Sendmax(USD(10)), Domain(domainID));
        env.close();
        BEAST_EXPECT(!offerExists(env, bob_, bobOffer2Seq));
    }

    void
    testRemoveUnfundedOffer(FeatureBitset features)
    {
        testcase("Remove unfunded offer");

        // checking that an unfunded offer will be implicitly removed by a
        // successful payment tx
        Env env(*this, features);
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
            PermissionedDEX(env);

        auto const aliceOfferSeq{env.seq(alice_)};
        env(offer(alice_, XRP(100), USD(100)), Domain(domainID));
        env.close();

        auto const bobOfferSeq{env.seq(bob_)};
        env(offer(bob_, XRP(20), USD(20)), Domain(domainID));
        env.close();

        BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(20), USD(20), 0, true));
        BEAST_EXPECT(checkOffer(env, alice_, aliceOfferSeq, XRP(100), USD(100), 0, true));

        auto const domainDirKey = getDefaultOfferDirKey(env, bob_, bobOfferSeq);
        BEAST_EXPECT(domainDirKey);
        BEAST_EXPECT(checkDirectorySize(
            env, *domainDirKey, 2));  // NOLINT(bugprone-unchecked-optional-access)

        // remove alice_ from domain and thus alice_'s offer becomes unfunded
        env(credentials::deleteCred(domainOwner, alice_, domainOwner, credType));
        env.close();

        env(pay(gw_, carol_, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
        env.close();

        BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, true));

        // alice_'s unfunded offer is removed implicitly
        BEAST_EXPECT(!offerExists(env, alice_, aliceOfferSeq));
        BEAST_EXPECT(checkDirectorySize(
            env, *domainDirKey, 1));  // NOLINT(bugprone-unchecked-optional-access)
    }

    void
    testAmmNotUsed(FeatureBitset features)
    {
        testcase("AMM not used");

        Env env(*this, features);
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
            PermissionedDEX(env);
        AMM const amm(env, alice_, XRP(10), USD(50));

        // a domain payment isn't able to consume AMM
        env(pay(bob_, carol_, USD(5)),
            Path(~USD),
            Sendmax(XRP(5)),
            Domain(domainID),
            Ter(tecPATH_PARTIAL));
        env.close();

        // a non domain payment can use AMM
        env(pay(bob_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)));
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
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            env(offer(bob_, XRP(10), USD(10)),
                Domain(domainID),
                Txflags(tfHybrid),
                Ter(temDISABLED));
            env.close();

            env(offer(bob_, XRP(10), USD(10)), Txflags(tfHybrid), Ter(temINVALID_FLAG));
            env.close();

            env.enableFeature(featurePermissionedDEX);
            env.close();

            // hybrid offer must have domainID
            env(offer(bob_, XRP(10), USD(10)), Txflags(tfHybrid), Ter(temINVALID_FLAG));
            env.close();

            // hybrid offer must have domainID
            auto const offerSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, offerSeq, XRP(10), USD(10), lsfHybrid, true));
        }

        // apply - domain offer can cross with hybrid
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));
            BEAST_EXPECT(offerExists(env, bob_, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, bob_) == 3);

            auto const aliceOfferSeq{env.seq(alice_)};
            env(offer(alice_, USD(10), XRP(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(!offerExists(env, alice_, aliceOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob_, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, alice_) == 2);
        }

        // apply - open offer can cross with hybrid
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            BEAST_EXPECT(offerExists(env, bob_, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, bob_) == 3);
            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));

            auto const aliceOfferSeq{env.seq(alice_)};
            env(offer(alice_, USD(10), XRP(10)));
            env.close();

            BEAST_EXPECT(!offerExists(env, alice_, aliceOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob_, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, alice_) == 2);
        }

        // apply - by default, hybrid offer tries to cross with offers in the
        // domain book
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, true));
            BEAST_EXPECT(ownerCount(env, bob_) == 3);

            // hybrid offer auto crosses with domain offer
            auto const aliceOfferSeq{env.seq(alice_)};
            env(offer(alice_, USD(10), XRP(10)), Domain(domainID), Txflags(tfHybrid));
            env.close();

            BEAST_EXPECT(!offerExists(env, alice_, aliceOfferSeq));
            BEAST_EXPECT(!offerExists(env, bob_, bobOfferSeq));
            BEAST_EXPECT(ownerCount(env, alice_) == 2);
        }

        // apply - hybrid offer does not automatically cross with open offers
        // because by default, it only tries to cross domain offers
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const bobOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, false));
            BEAST_EXPECT(ownerCount(env, bob_) == 3);

            // hybrid offer auto crosses with domain offer
            auto const aliceOfferSeq{env.seq(alice_)};
            env(offer(alice_, USD(10), XRP(10)), Domain(domainID), Txflags(tfHybrid));
            env.close();

            BEAST_EXPECT(offerExists(env, alice_, aliceOfferSeq));
            BEAST_EXPECT(offerExists(env, bob_, bobOfferSeq));
            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), 0, false));
            BEAST_EXPECT(checkOffer(env, alice_, aliceOfferSeq, USD(10), XRP(10), lsfHybrid, true));
            BEAST_EXPECT(ownerCount(env, alice_) == 3);
        }
    }

    void
    testHybridInvalidOffer(FeatureBitset features)
    {
        testcase("Hybrid invalid offer");

        // bob_ has a hybrid offer and then he is removed from domain.
        // in this case, the hybrid offer will be considered as unfunded even in
        // a regular payment
        Env env(*this, features);
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
            PermissionedDEX(env);

        auto const hybridOfferSeq{env.seq(bob_)};
        env(offer(bob_, XRP(50), USD(50)), Txflags(tfHybrid), Domain(domainID));
        env.close();

        // remove bob_ from domain
        env(credentials::deleteCred(domainOwner, bob_, domainOwner, credType));
        env.close();

        // bob_'s hybrid offer is unfunded and can not be consumed in a domain
        // payment
        env(pay(alice_, carol_, USD(5)),
            Path(~USD),
            Sendmax(XRP(5)),
            Domain(domainID),
            Ter(tecPATH_PARTIAL));
        env.close();
        BEAST_EXPECT(checkOffer(env, bob_, hybridOfferSeq, XRP(50), USD(50), lsfHybrid, true));

        // bob_'s unfunded hybrid offer can't be consumed even with a regular
        // payment
        env(pay(alice_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)), Ter(tecPATH_PARTIAL));
        env.close();
        BEAST_EXPECT(checkOffer(env, bob_, hybridOfferSeq, XRP(50), USD(50), lsfHybrid, true));

        // create a regular offer
        auto const regularOfferSeq{env.seq(bob_)};
        env(offer(bob_, XRP(10), USD(10)));
        env.close();
        BEAST_EXPECT(offerExists(env, bob_, regularOfferSeq));
        BEAST_EXPECT(checkOffer(env, bob_, regularOfferSeq, XRP(10), USD(10)));

        auto const sleHybridOffer = env.le(keylet::offer(bob_.id(), hybridOfferSeq));
        BEAST_EXPECT(sleHybridOffer);
        auto const openDir =
            sleHybridOffer->getFieldArray(sfAdditionalBooks)[0].getFieldH256(sfBookDirectory);
        BEAST_EXPECT(checkDirectorySize(env, openDir, 2));

        // this normal payment should consume the regular offer and remove the
        // unfunded hybrid offer
        env(pay(alice_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)));
        env.close();

        BEAST_EXPECT(!offerExists(env, bob_, hybridOfferSeq));
        BEAST_EXPECT(checkOffer(env, bob_, regularOfferSeq, XRP(5), USD(5)));
        BEAST_EXPECT(checkDirectorySize(env, openDir, 1));
    }

    void
    testHybridBookStep(FeatureBitset features)
    {
        testcase("Hybrid book step");

        // both non domain and domain payments can consume hybrid offer
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const hybridOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            env(pay(alice_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, hybridOfferSeq, XRP(5), USD(5), lsfHybrid, true));

            // hybrid offer can't be consumed since bob_ is not in domain anymore
            env(pay(alice_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob_, hybridOfferSeq));
        }

        // someone from another domain can't cross hybrid if they specified
        // wrong domainID
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            // Fund accounts
            Account const badDomainOwner("badDomainOwner");
            Account const devin("devin");
            env.fund(XRP(1000), badDomainOwner, devin);
            env.close();

            auto const badCredType = "badCred";
            pdomain::Credentials const credentials{{badDomainOwner, badCredType}};
            env(pdomain::setTx(badDomainOwner, credentials));

            auto objects = pdomain::getObjects(badDomainOwner, env);
            auto const badDomainID = objects.begin()->first;

            env(credentials::create(devin, badDomainOwner, badCredType));
            env.close();
            env(credentials::accept(devin, badDomainOwner, badCredType));
            env.close();

            auto const hybridOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            // other domains can't consume the offer
            env(pay(devin, badDomainOwner, USD(5)),
                Path(~USD),
                Sendmax(XRP(5)),
                Domain(badDomainID),
                Ter(tecPATH_DRY));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, hybridOfferSeq, XRP(10), USD(10), lsfHybrid, true));

            env(pay(alice_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)), Domain(domainID));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, hybridOfferSeq, XRP(5), USD(5), lsfHybrid, true));

            // hybrid offer can't be consumed since bob_ is not in domain anymore
            env(pay(alice_, carol_, USD(5)), Path(~USD), Sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob_, hybridOfferSeq));
        }

        // test domain payment consuming two offers w/ hybrid offer
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const eur = gw_["EUR"];
            env.trust(eur(1000), alice_);
            env.close();
            env.trust(eur(1000), bob_);
            env.close();
            env.trust(eur(1000), carol_);
            env.close();
            env(pay(gw_, bob_, eur(100)));
            env.close();

            auto const usdOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, usdOfferSeq, XRP(10), USD(10), 0, true));

            // payment fail because there isn't eur offer
            env(pay(alice_, carol_, eur(5)),
                Path(~USD, ~eur),
                Sendmax(XRP(5)),
                Domain(domainID),
                Ter(tecPATH_PARTIAL));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, usdOfferSeq, XRP(10), USD(10), 0, true));

            // bob_ creates a hybrid eur offer
            auto const eurOfferSeq{env.seq(bob_)};
            env(offer(bob_, USD(10), eur(10)), Domain(domainID), Txflags(tfHybrid));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, eurOfferSeq, USD(10), eur(10), lsfHybrid, true));

            // alice_ successfully consume two domain offers: xrp/usd and usd/eur
            env(pay(alice_, carol_, eur(5)), Path(~USD, ~eur), Sendmax(XRP(5)), Domain(domainID));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, usdOfferSeq, XRP(5), USD(5), 0, true));
            BEAST_EXPECT(checkOffer(env, bob_, eurOfferSeq, USD(5), eur(5), lsfHybrid, true));
        }

        // test regular payment using a regular offer and a hybrid offer
        {
            Env env(*this, features);
            auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
                PermissionedDEX(env);

            auto const eur = gw_["EUR"];
            env.trust(eur(1000), alice_);
            env.close();
            env.trust(eur(1000), bob_);
            env.close();
            env.trust(eur(1000), carol_);
            env.close();
            env(pay(gw_, bob_, eur(100)));
            env.close();

            // bob_ creates a regular usd offer
            auto const usdOfferSeq{env.seq(bob_)};
            env(offer(bob_, XRP(10), USD(10)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, usdOfferSeq, XRP(10), USD(10), 0, false));

            // bob_ creates a hybrid eur offer
            auto const eurOfferSeq{env.seq(bob_)};
            env(offer(bob_, USD(10), eur(10)), Domain(domainID), Txflags(tfHybrid));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob_, eurOfferSeq, USD(10), eur(10), lsfHybrid, true));

            // alice_ successfully consume two offers: xrp/usd and usd/eur
            env(pay(alice_, carol_, eur(5)), Path(~USD, ~eur), Sendmax(XRP(5)));
            env.close();

            BEAST_EXPECT(checkOffer(env, bob_, usdOfferSeq, XRP(5), USD(5), 0, false));
            BEAST_EXPECT(checkOffer(env, bob_, eurOfferSeq, USD(5), eur(5), lsfHybrid, true));
        }
    }

    void
    testHybridOfferDirectories(FeatureBitset features)
    {
        Env env(*this, features);
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
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
            auto const bobOfferSeq{env.seq(bob_)};
            offerSeqs.emplace_back(bobOfferSeq);
            env(offer(bob_, XRP(10), USD(10)), Txflags(tfHybrid), Domain(domainID));
            env.close();

            auto const sleOffer = env.le(keylet::offer(bob_.id(), bobOfferSeq));
            BEAST_EXPECT(sleOffer);
            BEAST_EXPECT(sleOffer->getFieldH256(sfBookDirectory) == domainDir);
            BEAST_EXPECT(sleOffer->getFieldArray(sfAdditionalBooks).size() == 1);
            BEAST_EXPECT(
                sleOffer->getFieldArray(sfAdditionalBooks)[0].getFieldH256(sfBookDirectory) ==
                openDir);

            BEAST_EXPECT(checkOffer(env, bob_, bobOfferSeq, XRP(10), USD(10), lsfHybrid, true));
            BEAST_EXPECT(checkDirectorySize(env, domainDir, i));
            BEAST_EXPECT(checkDirectorySize(env, openDir, i));
        }

        for (auto const offerSeq : offerSeqs)
        {
            env(offerCancel(bob_, offerSeq));
            env.close();
            dirCnt--;
            BEAST_EXPECT(!offerExists(env, bob_, offerSeq));
            BEAST_EXPECT(checkDirectorySize(env, domainDir, dirCnt));
            BEAST_EXPECT(checkDirectorySize(env, openDir, dirCnt));
        }
    }

    void
    testAutoBridge(FeatureBitset features)
    {
        testcase("Auto bridge");

        Env env(*this, features);
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] =
            PermissionedDEX(env);
        auto const eur = gw_["EUR"];

        for (auto const& account : {alice_, bob_, carol_})
        {
            env(trust(account, eur(10000)));
            env.close();
        }

        env(pay(gw_, carol_, eur(1)));
        env.close();

        auto const aliceOfferSeq{env.seq(alice_)};
        auto const bobOfferSeq{env.seq(bob_)};
        env(offer(alice_, XRP(100), USD(1)), Domain(domainID));
        env(offer(bob_, eur(1), XRP(100)), Domain(domainID));
        env.close();

        // carol_'s offer should cross bob_ and alice_'s offers due to auto
        // bridging
        auto const carolOfferSeq{env.seq(carol_)};
        env(offer(carol_, USD(1), eur(1)), Domain(domainID));
        env.close();

        BEAST_EXPECT(!offerExists(env, bob_, aliceOfferSeq));
        BEAST_EXPECT(!offerExists(env, bob_, bobOfferSeq));
        BEAST_EXPECT(!offerExists(env, bob_, carolOfferSeq));
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
        testHybridMalformedOffer(all);
        testHybridMalformedOffer(all - fixCleanup3_1_3);

        // Cancelling a regular offer in a domain OfferCreate is allowed
        // only after fixCleanup3_2_0.
        testCancelRegularOfferWithDomainCreate(all);
        testCancelRegularOfferWithDomainCreate(all - fixCleanup3_2_0);
    }
};

BEAST_DEFINE_TESTSUITE(PermissionedDEX, app, xrpl);

}  // namespace xrpl::test
