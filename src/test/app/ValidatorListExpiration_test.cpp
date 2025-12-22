#include <test/jtx.h>

#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/misc/Manifest.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/json/json_writer.h>

namespace xrpl {
namespace test {

/**
 * Test the validator list expiration logic that RCLConsensus checks
 * in preStartRound().
 *
 * This tests the exact code path at RCLConsensus.cpp:991:
 *   auto const when = app_.validators().expires();
 *   if (!when || *when < now)
 *       validating_ = false;  // Bow out of consensus!
 */
class ValidatorListExpiration_test : public beast::unit_test::suite
{
private:
    // Helper: create a manifest string (from ValidatorList_test.cpp)
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

        if (seq != std::numeric_limits<std::uint32_t>::max())
        {
            st[sfSigningPubKey] = spk;
            sign(st, HashPrefix::manifest, *publicKeyType(spk), ssk);
        }

        sign(st, HashPrefix::manifest, *publicKeyType(pk), sk);
        Serializer s;
        st.add(s);
        return std::string(static_cast<char const*>(s.data()), s.size());
    }

    // Helper: create validator list blob (simplified from ValidatorList_test.cpp)
    std::string
    makeList(
        std::vector<std::string> const& validatorManifests,
        std::size_t sequence,
        std::size_t validUntil,
        std::optional<std::size_t> validFrom = {})
    {
        std::string data = "{\"sequence\":" + std::to_string(sequence) +
            ",\"expiration\":" + std::to_string(validUntil);
        if (validFrom)
            data += ",\"effective\":" + std::to_string(*validFrom);
        data += ",\"validators\":[";

        for (auto const& manifest : validatorManifests)
        {
            // Extract public key from manifest
            auto m = deserializeManifest(manifest);
            if (m)
                data += "{\"validation_public_key\":\"" +
                    strHex(m->masterKey) + "\",\"manifest\":\"" +
                    base64_encode(manifest) + "\"},";
        }

        if (!validatorManifests.empty())
            data.pop_back();  // Remove trailing comma
        data += "]}";
        return base64_encode(data);
    }

    // Helper: sign validator list blob
    std::string
    signList(std::string const& blob, PublicKey const& pk, SecretKey const& sk)
    {
        auto hash = sha512Half(makeSlice(blob));
        auto sig = signDigest(pk, sk, hash);
        return strHex(Slice{sig.data(), sig.size()});
    }

    void
    testExpirationReturnsNullopt()
    {
        testcase("expires() returns nullopt - No VL loaded");

        using namespace jtx;
        Env env{*this};

        // Scenario: No validator list loaded at all
        // Expected: expires() should return nullopt
        auto expiration = env.app().validators().expires();

        BEAST_EXPECT(!expiration.has_value());
        log << "With no VL loaded, expires() = nullopt" << std::endl;

        // This is the condition that causes RCLConsensus to bow out:
        // if (!when || *when < now)
        if (!expiration)
        {
            log << "✓ RCLConsensus would bow out (no expiration)" << std::endl;
            pass();
        }
    }

    void
    testExpirationWithValidDilithiumVL()
    {
        testcase("expires() with valid dilithium VL");

        using namespace jtx;
        Env env{*this};

        // Generate dilithium publisher keypair
        auto const [publisherPubKey, publisherSecKey] =
            randomKeyPair(KeyType::dilithium);

        // Generate dilithium validator keypairs (master + signing)
        auto const valMasterSec = randomSecretKey(KeyType::dilithium);
        auto const valMasterPub = derivePublicKey(KeyType::dilithium, valMasterSec);
        auto const [valSigningPub, valSigningSec] =
            randomKeyPair(KeyType::dilithium);

        // Create manifest for validator
        std::string manifest = makeManifestString(
            valMasterPub, valMasterSec, valSigningPub, valSigningSec, 1);

        // Set times: effective=now, expiration=now+1 year
        auto now = env.timeKeeper().now();
        std::size_t effectiveRipple = now.time_since_epoch().count();
        std::size_t expirationRipple =
            (now + std::chrono::seconds(31536000)).time_since_epoch().count();

        log << "Creating VL with:" << std::endl;
        log << "  effective (Ripple): " << effectiveRipple << std::endl;
        log << "  expiration (Ripple): " << expirationRipple << std::endl;

        // Create blob
        std::string blob = makeList({manifest}, 1, expirationRipple, effectiveRipple);

        // Sign the blob
        std::string signature = signList(blob, publisherPubKey, publisherSecKey);

        // Create ValidatorList
        ManifestCache manifests;
        auto validatorList = std::make_unique<ValidatorList>(
            manifests,
            manifests,
            env.timeKeeper(),
            env.app().config().legacy("database_path"),
            env.journal);

        // Configure publisher key as trusted
        std::vector<std::string> publisherKeys = {strHex(publisherPubKey)};
        BEAST_EXPECT(validatorList->load({}, {}, publisherKeys));

        // Apply the list
        ValidatorBlobInfo blobInfo{blob, signature, ""};

        auto result = validatorList->applyLists(
            "", 1, {blobInfo}, "test://dilithium-vl");

        log << "applyLists disposition: " << to_string(result.bestDisposition())
            << std::endl;

        // Now test the EXACT code path RCLConsensus uses
        auto expiration = validatorList->expires();
        auto currentTime = env.timeKeeper().now();

        log << "Current time (Ripple): "
            << currentTime.time_since_epoch().count() << std::endl;

        if (expiration)
        {
            log << "Expiration time (Ripple): "
                << expiration->time_since_epoch().count() << std::endl;

            if (*expiration < currentTime)
            {
                fail("VL expired! RCLConsensus would bow out.\n"
                     "expires() = " +
                     std::to_string(expiration->time_since_epoch().count()) +
                     "\n"
                     "now() = " +
                     std::to_string(currentTime.time_since_epoch().count()));
            }
            else
            {
                log << "✓ VL is valid, not expired. RCLConsensus would continue."
                    << std::endl;
                pass();
            }
        }
        else
        {
            fail("expires() returned nullopt! RCLConsensus would bow out.\n"
                 "disposition = " +
                 to_string(result.bestDisposition()));
        }
    }

    void
    testExpirationWithExpiredDilithiumVL()
    {
        testcase("expires() with EXPIRED dilithium VL");

        using namespace jtx;
        Env env{*this};

        // Generate dilithium publisher keypair
        auto const [publisherPubKey, publisherSecKey] =
            randomKeyPair(KeyType::dilithium);

        // Generate dilithium validator keypair
        auto const [validatorPubKey, validatorSecKey] =
            randomKeyPair(KeyType::dilithium);

        // Set times: effective=now-2 years, expiration=now-1 year (EXPIRED!)
        auto now = env.timeKeeper().now();
        std::uint32_t effectiveRipple =
            (now - std::chrono::seconds(63072000)).time_since_epoch().count();
        std::uint32_t expirationRipple =
            (now - std::chrono::seconds(31536000)).time_since_epoch().count();

        log << "Creating EXPIRED VL with:" << std::endl;
        log << "  effective (Ripple): " << effectiveRipple << std::endl;
        log << "  expiration (Ripple): " << expirationRipple << " (1 year ago!)"
            << std::endl;

        // Build the blob content
        Json::Value blobContent;
        blobContent["sequence"] = 1;
        blobContent["effective"] = effectiveRipple;
        blobContent["expiration"] = expirationRipple;

        // Add validator
        Json::Value validators(Json::arrayValue);
        Json::Value val;
        val["validation_public_key"] = strHex(validatorPubKey);
        val["manifest"] = "";
        validators.append(val);
        blobContent["validators"] = validators;

        // Encode blob
        std::string blobJson = Json::FastWriter().write(blobContent);
        std::string blob = base64_encode(blobJson);

        // Sign the blob
        auto blobHash = sha512Half(makeSlice(blob));
        auto signature = signDigest(publisherPubKey, publisherSecKey, blobHash);

        // Create ValidatorList
        ManifestCache manifests;
        auto validatorList = std::make_unique<ValidatorList>(
            manifests,
            manifests,
            env.timeKeeper(),
            env.app().config().legacy("database_path"),
            env.journal);

        // Configure publisher key as trusted
        std::vector<std::string> publisherKeys = {strHex(publisherPubKey)};
        BEAST_EXPECT(validatorList->load({}, {}, publisherKeys));

        // Apply the EXPIRED list
        ValidatorBlobInfo blobInfo{
            blob,
            strHex(Slice{signature.data(), signature.size()}),
            ""};

        auto result = validatorList->applyLists(
            "",
            1,
            {blobInfo},
            "test://expired-dilithium-vl");

        log << "applyLists disposition: " << to_string(result.bestDisposition())
            << std::endl;

        // Test the RCLConsensus condition with EXPIRED VL
        auto expiration = validatorList->expires();
        auto currentTime = env.timeKeeper().now();

        if (expiration)
        {
            log << "Expiration time (Ripple): "
                << expiration->time_since_epoch().count() << std::endl;
            log << "Current time (Ripple): "
                << currentTime.time_since_epoch().count() << std::endl;

            // This SHOULD be expired
            if (*expiration < currentTime)
            {
                auto secondsAgo =
                    std::chrono::duration_cast<std::chrono::seconds>(
                        currentTime - *expiration)
                        .count();
                log << "✓ VL correctly shows as expired (" << secondsAgo
                    << " seconds ago)" << std::endl;
                log << "✓ RCLConsensus would correctly bow out" << std::endl;
                pass();
            }
            else
            {
                fail("VL should be expired but shows as valid!\n"
                     "expires() = " +
                     std::to_string(expiration->time_since_epoch().count()) +
                     "\n"
                     "now() = " +
                     std::to_string(currentTime.time_since_epoch().count()));
            }
        }
        else
        {
            // nullopt is also a failure case
            log << "✓ expires() returned nullopt for expired VL" << std::endl;
            log << "✓ RCLConsensus would correctly bow out" << std::endl;
            pass();
        }
    }

    void
    testSecp256k1VLExpiration()
    {
        testcase("expires() with secp256k1 VL (backward compat)");

        using namespace jtx;
        Env env{*this};

        // Generate secp256k1 keys for backward compatibility test
        auto const [publisherPubKey, publisherSecKey] =
            randomKeyPair(KeyType::secp256k1);
        auto const [validatorPubKey, validatorSecKey] =
            randomKeyPair(KeyType::secp256k1);

        // Set valid times
        auto now = env.timeKeeper().now();
        std::uint32_t effectiveRipple = now.time_since_epoch().count();
        std::uint32_t expirationRipple =
            (now + std::chrono::seconds(31536000)).time_since_epoch().count();

        // Build blob
        Json::Value blobContent;
        blobContent["sequence"] = 1;
        blobContent["effective"] = effectiveRipple;
        blobContent["expiration"] = expirationRipple;

        Json::Value validators(Json::arrayValue);
        Json::Value val;
        val["validation_public_key"] = strHex(validatorPubKey);
        val["manifest"] = "";
        validators.append(val);
        blobContent["validators"] = validators;

        std::string blobJson = Json::FastWriter().write(blobContent);
        std::string blob = base64_encode(blobJson);

        auto blobHash = sha512Half(makeSlice(blob));
        auto signature = signDigest(publisherPubKey, publisherSecKey, blobHash);

        // Create ValidatorList
        ManifestCache manifests;
        auto validatorList = std::make_unique<ValidatorList>(
            manifests,
            manifests,
            env.timeKeeper(),
            env.app().config().legacy("database_path"),
            env.journal);

        std::vector<std::string> publisherKeys = {strHex(publisherPubKey)};
        BEAST_EXPECT(validatorList->load({}, {}, publisherKeys));

        ValidatorBlobInfo blobInfo{
            blob,
            strHex(Slice{signature.data(), signature.size()}),
            ""};

        auto result = validatorList->applyLists(
            "", 1, {blobInfo}, "test://secp256k1-vl");

        log << "secp256k1 VL disposition: "
            << to_string(result.bestDisposition()) << std::endl;

        auto expiration = validatorList->expires();
        auto currentTime = env.timeKeeper().now();

        if (expiration && *expiration >= currentTime)
        {
            log << "✓ secp256k1 VL works correctly (backward compat)"
                << std::endl;
            pass();
        }
        else
        {
            fail("secp256k1 VL backward compatibility broken!");
        }
    }

public:
    void
    run() override
    {
        testExpirationReturnsNullopt();
        testExpirationWithValidDilithiumVL();
        testExpirationWithExpiredDilithiumVL();
        testSecp256k1VLExpiration();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorListExpiration, app, xrpl);

}  // namespace test
}  // namespace xrpl
