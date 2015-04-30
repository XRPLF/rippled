//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2014 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/overlay/impl/Manifest.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STExchange.h>

namespace ripple {
namespace tests {

class manifest_test : public ripple::TestSuite
{
public:
    // Return a manifest in both serialized and STObject form
    std::string
    make_manifest(AnySecretKey const& sk, AnyPublicKey const& spk, int seq, bool broken = false)
    {
        auto const pk = sk.publicKey();
        
        STObject st(sfGeneric);
        set(st, sfSequence, seq);
        set(st, sfPublicKey, pk);
        set(st, sfSigningPubKey, spk);

        sign(st, HashPrefix::manifest, sk);
        expect(verify(st, HashPrefix::manifest, pk));

        if (broken)
        {
            set(st, sfSequence, seq + 1);
        }

        Serializer s;
        st.add(s);

        std::string const m (static_cast<char const*> (s.data()), s.size());
        return m;
    }

    void
    run() override
    {
        auto const accepted  = ManifestDisposition::accepted;
        auto const malformed = ManifestDisposition::malformed;
        auto const untrusted = ManifestDisposition::untrusted;
        auto const stale     = ManifestDisposition::stale;
        auto const invalid   = ManifestDisposition::invalid;

        beast::Journal journal;

        auto const sk_a = AnySecretKey::make_ed25519();
        auto const sk_b = AnySecretKey::make_ed25519();
        auto const pk_a = sk_a.publicKey();
        auto const pk_b = sk_b.publicKey();
        auto const kp_a = AnySecretKey::make_secp256k1_pair();
        auto const kp_b = AnySecretKey::make_secp256k1_pair();

        auto const s_a0 = make_manifest(sk_a, kp_a.second, 0);
        auto const s_a1 = make_manifest(sk_a, kp_a.second, 1);
        auto const s_b0 = make_manifest(sk_b, kp_b.second, 0);
        auto const s_b1 = make_manifest(sk_b, kp_b.second, 1);
        auto const s_b2 = make_manifest(sk_b, kp_b.second, 2, true);  // broken
        auto const fake = s_b1 + '\0';

        ManifestCache cache;

        expect(cache.applyManifest(s_a0, journal) == untrusted, "have to install a trusted key first");

        cache.addTrustedKey(pk_a, "a");
        cache.addTrustedKey(pk_b, "b");

        expect(cache.applyManifest(s_a0, journal) == accepted);
        expect(cache.applyManifest(s_a0, journal) == stale);

        expect(cache.applyManifest(s_a1, journal) == accepted);
        expect(cache.applyManifest(s_a1, journal) == stale);
        expect(cache.applyManifest(s_a0, journal) == stale);

        expect(cache.applyManifest(s_b0, journal) == accepted);
        expect(cache.applyManifest(s_b0, journal) == stale);

        expect(cache.applyManifest(fake, journal) == malformed);
        expect(cache.applyManifest(s_b2, journal) == invalid);
    }
};

BEAST_DEFINE_TESTSUITE(manifest,overlay,ripple);

} // tests
} // ripple
