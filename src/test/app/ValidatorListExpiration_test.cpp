#include <test/jtx/Env.h>

#include <xrpld/app/misc/ValidatorList.h>

#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/server/Manifest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace xrpl {
namespace test {

/**
 * The validator list expiration RCLConsensus::Adaptor::preStartRound checks: a node stops
 * validating when expires() is nullopt or in the past.
 */
class ValidatorListExpiration_test : public beast::unit_test::Suite
{
    static std::string
    makeManifestString(
        PublicKey const& pk,
        SecretKey const& sk,
        PublicKey const& spk,
        SecretKey const& ssk,
        int seq)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;
        sign(st, HashPrefix::Manifest, *publicKeyType(spk), ssk);
        sign(st, HashPrefix::Manifest, *publicKeyType(pk), sk, sfMasterSignature);
        Serializer s;
        st.add(s);
        return std::string(static_cast<char const*>(s.data()), s.size());
    }

    struct Applied
    {
        ListDisposition disposition;
        std::optional<TimeKeeper::time_point> expires;
    };

    // Applies a list of one validator, published and listed with keys of `type`, valid from
    // `effective` to `expiration`.
    Applied
    applyList(
        jtx::Env& env,
        KeyType type,
        TimeKeeper::time_point effective,
        TimeKeeper::time_point expiration)
    {
        auto const publisherSecret = randomSecretKey(type);
        auto const publisherPublic = derivePublicKey(type, publisherSecret);
        auto const publisherSigning = randomKeyPair(type);
        auto const publisherManifest = base64Encode(makeManifestString(
            publisherPublic, publisherSecret, publisherSigning.first, publisherSigning.second, 1));

        auto const validatorSecret = randomSecretKey(type);
        auto const validatorPublic = derivePublicKey(type, validatorSecret);
        auto const validatorSigning = randomKeyPair(type);
        auto const validatorManifest = base64Encode(makeManifestString(
            validatorPublic, validatorSecret, validatorSigning.first, validatorSigning.second, 1));

        std::string const data = "{\"sequence\":1,\"effective\":" +
            std::to_string(effective.time_since_epoch().count()) +
            ",\"expiration\":" + std::to_string(expiration.time_since_epoch().count()) +
            ",\"validators\":[{\"validation_public_key\":\"" + strHex(validatorPublic) +
            "\",\"manifest\":\"" + validatorManifest + "\"}]}";
        auto const blob = base64Encode(data);
        auto const signature =
            strHex(sign(publisherSigning.first, publisherSigning.second, makeSlice(data)));

        ManifestCache manifests;
        auto validatorList = std::make_unique<ValidatorList>(
            manifests,
            manifests,
            env.timeKeeper(),
            env.app().config().legacy("database_path"),
            env.journal);
        std::vector<std::string> const noKeys;
        std::vector<std::string> const publisherKeys{strHex(publisherPublic)};
        BEAST_EXPECT(validatorList->load({}, noKeys, publisherKeys));

        auto const result = validatorList->applyLists(
            env.current()->rules(),
            publisherManifest,
            1,
            {{blob, signature, {}}},
            "test://expiration");
        return {result.bestDisposition(), validatorList->expires()};
    }

    void
    testExpirationReturnsNullopt()
    {
        testcase("expires() is nullopt with no list loaded");

        jtx::Env env{*this};
        BEAST_EXPECT(!env.app().getValidators().expires());
    }

    void
    testValidList(KeyType type)
    {
        testcase(std::string("expires() of a valid ") + to_string(type) + " list");

        using namespace std::chrono_literals;
        jtx::Env env{*this};
        auto const now = env.timeKeeper().now();
        auto const expiration = now + 365 * 24h;

        auto const applied = applyList(env, type, now, expiration);
        BEAST_EXPECT(applied.disposition == ListDisposition::Accepted);
        BEAST_EXPECT(applied.expires == expiration);
    }

    void
    testExpiredList()
    {
        testcase("expires() of an expired dilithium list");

        using namespace std::chrono_literals;
        jtx::Env env{*this};
        // NetClock counts unsigned seconds from 2000, so the clock moves past two years first
        env.timeKeeper().set(env.timeKeeper().now() + 800 * 24h);
        auto const now = env.timeKeeper().now();

        auto const applied = applyList(env, KeyType::Dilithium, now - 730 * 24h, now - 365 * 24h);
        BEAST_EXPECT(applied.disposition == ListDisposition::Expired);
        BEAST_EXPECT(!applied.expires || *applied.expires < now);
    }

public:
    void
    run() override
    {
        testExpirationReturnsNullopt();
        testValidList(KeyType::Dilithium);
        testExpiredList();
        testValidList(KeyType::Secp256k1);
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorListExpiration, app, xrpl);

}  // namespace test
}  // namespace xrpl
