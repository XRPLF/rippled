// Unit tests for the untrusted-manifest cap in ManifestCache.
//
// These exercise the bound added to stop a peer flooding the cache with
// manifests for validators this node does not list:
//   - a brand-new unlisted key is rejected once the cap is full,
//   - existing entries (including revocations) are never evicted,
//   - revocations of brand-new unlisted keys count against the cap,
//   - a revocation that updates a known key is never capped,
//   - listed/trusted manifests bypass the cap, and
//   - promoteToTrusted frees an untrusted slot when a key becomes listed.
//
// Scope: the ManifestCache cap mechanics only. The relay/persist gating in
// OverlayImpl::onManifests and the wallet-DB persistence rule in Wallet.cpp are
// part of the same fix but live in the overlay/wallet layers and are not
// covered here.

#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/server/Manifest.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using namespace xrpl;

namespace {

// A master key plus the secret needed to sign manifests for it.
struct Validator
{
    SecretKey masterSecret;
    PublicKey masterPublic;
};

Validator
makeValidator()
{
    auto const [pk, sk] = randomKeyPair(KeyType::Ed25519);
    return Validator{.masterSecret = sk, .masterPublic = pk};
}

// Build a signed, non-revocation manifest for the given validator at the given
// sequence, with a fresh ephemeral signing key.
Manifest
makeManifest(Validator const& v, std::uint32_t seq)
{
    auto const [spk, ssk] = randomKeyPair(KeyType::Secp256k1);

    STObject st(sfGeneric);
    st[sfSequence] = seq;
    st[sfPublicKey] = v.masterPublic;
    st[sfSigningPubKey] = spk;

    sign(st, HashPrefix::Manifest, KeyType::Secp256k1, ssk);
    sign(st, HashPrefix::Manifest, KeyType::Ed25519, v.masterSecret, sfMasterSignature);

    Serializer s;
    st.add(s);
    auto m = deserializeManifest(std::string(static_cast<char const*>(s.data()), s.size()));
    if (!m)
        throw std::runtime_error("test manifest failed to deserialize");
    return std::move(*m);
}

// Build a signed revocation manifest (sequence 0xFFFFFFFF, no signing key).
Manifest
makeRevocation(Validator const& v)
{
    STObject st(sfGeneric);
    st[sfSequence] = std::numeric_limits<std::uint32_t>::max();
    st[sfPublicKey] = v.masterPublic;

    sign(st, HashPrefix::Manifest, KeyType::Ed25519, v.masterSecret, sfMasterSignature);

    Serializer s;
    st.add(s);
    auto m = deserializeManifest(std::string(static_cast<char const*>(s.data()), s.size()));
    if (!m.has_value())
        throw std::runtime_error("test revocation failed to deserialize");
    return std::move(*m);
}

}  // namespace

// A brand-new untrusted key is accepted until the cap is reached, then
// rejected. Existing entries are not evicted, and the count never exceeds the
// cap.
TEST(ManifestCacheCap, rejects_new_untrusted_at_cap)
{
    ManifestCache cache(beast::Journal{beast::Journal::getNullSink()}, /*maxUntrustedCount*/ 2);

    auto const validator1 = makeValidator();
    auto const validator2 = makeValidator();
    auto const validator3 = makeValidator();

    EXPECT_EQ(
        cache.applyManifest(makeManifest(validator1, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(
        cache.applyManifest(makeManifest(validator2, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 2u);

    // Third distinct unlisted key is over the cap and is rejected, not stored.
    EXPECT_EQ(
        cache.applyManifest(makeManifest(validator3, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::UntrustedCapacity);
    EXPECT_EQ(cache.untrustedKeyCount(), 2u);
    // The rejected key was not filed under the uncapped side either, so the
    // cache holds exactly the two accepted keys.
    EXPECT_EQ(cache.uncappedKeyCount(), 0u);

    // The first key is still cached (nothing was evicted to make room).
    // getManifest returns nullopt for revoked keys, so this presence check only
    // works because validator1 and validator3 got non-revocation manifests.
    EXPECT_TRUE(cache.getManifest(validator1.masterPublic).has_value());
    EXPECT_FALSE(cache.getManifest(validator3.masterPublic).has_value());
}

// A higher-sequence manifest for a key already cached is an update, not a new
// entry, so it is accepted even when the cap is full and does not change the
// count.
TEST(ManifestCacheCap, update_to_known_key_bypasses_cap)
{
    ManifestCache cache(beast::Journal{beast::Journal::getNullSink()}, /*maxUntrustedCount*/ 1);

    auto const a = makeValidator();
    EXPECT_EQ(
        cache.applyManifest(makeManifest(a, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);

    // Cap is now full. An update to the same key still applies.
    EXPECT_EQ(
        cache.applyManifest(makeManifest(a, 2), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    EXPECT_EQ(cache.getSequence(a.masterPublic), 2u);
}

// A revocation for a brand-new unlisted key is a new entry, so it counts
// against the cap and is rejected once full.
TEST(ManifestCacheCap, new_untrusted_revocation_counts_against_cap)
{
    ManifestCache cache(beast::Journal{beast::Journal::getNullSink()}, /*maxUntrustedCount*/ 1);

    auto const a = makeValidator();
    auto const b = makeValidator();

    EXPECT_EQ(
        cache.applyManifest(makeRevocation(a), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    EXPECT_TRUE(cache.revoked(a.masterPublic));

    // Second new revocation is over the cap and rejected.
    EXPECT_EQ(
        cache.applyManifest(makeRevocation(b), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::UntrustedCapacity);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    // revoked() also returns false for a key the cache never saw, so pin the
    // total size to show b was not stored at all.
    EXPECT_EQ(cache.uncappedKeyCount(), 0u);
    EXPECT_FALSE(cache.revoked(b.masterPublic));
}

// A revocation that supersedes a key already in the cache is an update and is
// always accepted, even when the cap is full. The key stays revoked.
TEST(ManifestCacheCap, revocation_of_known_key_always_accepted)
{
    ManifestCache cache(beast::Journal{beast::Journal::getNullSink()}, /*maxUntrustedCount*/ 1);

    auto const a = makeValidator();
    EXPECT_EQ(
        cache.applyManifest(makeManifest(a, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    EXPECT_FALSE(cache.revoked(a.masterPublic));

    // Cap is full, but revoking the known key still applies.
    EXPECT_EQ(
        cache.applyManifest(makeRevocation(a), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    EXPECT_TRUE(cache.revoked(a.masterPublic));
}

// A trusted manifest is never counted against the cap, no matter how full it
// is. Trusted keys are unbounded.
TEST(ManifestCacheCap, trusted_manifest_bypasses_cap)
{
    ManifestCache cache(beast::Journal{beast::Journal::getNullSink()}, /*maxUntrustedCount*/ 1);

    auto const a = makeValidator();
    auto const trusted = makeValidator();

    EXPECT_EQ(
        cache.applyManifest(makeManifest(a, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    EXPECT_EQ(cache.uncappedKeyCount(), 0u);

    // Cap is full, but a trusted manifest is accepted and not counted.
    EXPECT_EQ(
        cache.applyManifest(makeManifest(trusted, 1), ManifestRateLimitCapPolicy::Uncapped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    // The trusted key was stored on the uncapped side, so the cache grew
    // without the capped count moving.
    EXPECT_EQ(cache.uncappedKeyCount(), 1u);
    EXPECT_TRUE(cache.getManifest(trusted.masterPublic).has_value());
}

// promoteToTrusted frees the untrusted slot of a key that becomes listed. It is
// idempotent and a no-op for keys that were never counted.
TEST(ManifestCacheCap, promotion_frees_slot)
{
    ManifestCache cache(beast::Journal{beast::Journal::getNullSink()}, /*maxUntrustedCount*/ 1);

    auto const a = makeValidator();
    auto const b = makeValidator();
    auto const never = makeValidator();

    EXPECT_EQ(
        cache.applyManifest(makeManifest(a, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    EXPECT_EQ(cache.uncappedKeyCount(), 0u);

    // Promoting a key that was never counted does nothing.
    cache.promoteToTrusted(never.masterPublic);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    // Promotion does not invent a cache entry for an unknown key.
    EXPECT_EQ(cache.uncappedKeyCount(), 0u);

    // Promote the counted key: its slot is freed.
    cache.promoteToTrusted(a.masterPublic);
    EXPECT_EQ(cache.untrustedKeyCount(), 0u);
    // The key moved sides rather than being dropped.
    EXPECT_EQ(cache.uncappedKeyCount(), 1u);

    // Idempotent: promoting again does not underflow or change the count.
    cache.promoteToTrusted(a.masterPublic);
    EXPECT_EQ(cache.untrustedKeyCount(), 0u);
    EXPECT_EQ(cache.uncappedKeyCount(), 1u);

    // The freed slot can now hold another untrusted key.
    EXPECT_EQ(
        cache.applyManifest(makeManifest(b, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    EXPECT_EQ(cache.uncappedKeyCount(), 1u);
    // The promoted key is still cached.
    EXPECT_TRUE(cache.getManifest(a.masterPublic).has_value());
}

// A key first cached as untrusted, then re-applied as trusted, has its slot
// freed by applyManifest itself (without a separate promoteToTrusted call).
TEST(ManifestCacheCap, trusted_update_releases_untrusted_slot)
{
    ManifestCache cache(beast::Journal{beast::Journal::getNullSink()}, /*maxUntrustedCount*/ 1);

    auto const a = makeValidator();

    EXPECT_EQ(
        cache.applyManifest(makeManifest(a, 1), ManifestRateLimitCapPolicy::Capped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 1u);
    EXPECT_EQ(cache.uncappedKeyCount(), 0u);

    // Same key, higher sequence, now trusted: the untrusted slot is released.
    EXPECT_EQ(
        cache.applyManifest(makeManifest(a, 2), ManifestRateLimitCapPolicy::Uncapped),
        ManifestDisposition::Accepted);
    EXPECT_EQ(cache.untrustedKeyCount(), 0u);
    // Still one entry, now on the uncapped side: this was an update, not a
    // second insert.
    EXPECT_EQ(cache.uncappedKeyCount(), 1u);
}

// A default-constructed cache enforces the production cap. Assert against the
// named constant, never a copied literal.
TEST(ManifestCacheCap, default_cap_uses_production_constant)
{
    ManifestCache const cache;
    EXPECT_EQ(cache.maxUntrustedCount(), kMaxUntrustedCount);
}
