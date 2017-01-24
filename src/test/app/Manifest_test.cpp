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

#include <ripple/app/misc/Manifest.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/StringUtilities.h>
#include <test/jtx.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STExchange.h>
#include <beast/core/detail/base64.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/utility/in_place_factory.hpp>

namespace ripple {
namespace test {

class Manifest_test : public beast::unit_test::suite
{
private:
    static PublicKey randomNode ()
    {
        return derivePublicKey (
            KeyType::secp256k1,
            randomSecretKey());
    }

    static PublicKey randomMasterKey ()
    {
        return derivePublicKey (
            KeyType::ed25519,
            randomSecretKey());
    }

    static void cleanupDatabaseDir (boost::filesystem::path const& dbPath)
    {
        using namespace boost::filesystem;
        if (!exists (dbPath) || !is_directory (dbPath) || !is_empty (dbPath))
            return;
        remove (dbPath);
    }

    static void setupDatabaseDir (boost::filesystem::path const& dbPath)
    {
        using namespace boost::filesystem;
        if (!exists (dbPath))
        {
            create_directory (dbPath);
            return;
        }

        if (!is_directory (dbPath))
        {
            // someone created a file where we want to put our directory
            Throw<std::runtime_error> ("Cannot create directory: " +
                                      dbPath.string ());
        }
    }
    static boost::filesystem::path getDatabasePath ()
    {
        return boost::filesystem::current_path () / "manifest_test_databases";
    }

public:
    Manifest_test ()
    {
        try
        {
            setupDatabaseDir (getDatabasePath ());
        }
        catch (std::exception const&)
        {
        }
    }
    ~Manifest_test ()
    {
        try
        {
            cleanupDatabaseDir (getDatabasePath ());
        }
        catch (std::exception const&)
        {
        }
    }

    std::string
    makeManifestString (
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

        sign(st, HashPrefix::manifest, *publicKeyType(spk), ssk);
        sign(st, HashPrefix::manifest, *publicKeyType(pk), sk,
            sfMasterSignature);

        Serializer s;
        st.add(s);

        return beast::detail::base64_encode (std::string(
            static_cast<char const*> (s.data()), s.size()));
    }

    Manifest
    make_Manifest
        (SecretKey const& sk, KeyType type, SecretKey const& ssk, KeyType stype,
         int seq, bool invalidSig = false)
    {
        auto const pk = derivePublicKey(type, sk);
        auto const spk = derivePublicKey(stype, ssk);

        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;

        sign(st, HashPrefix::manifest, stype, ssk);
        BEAST_EXPECT(verify(st, HashPrefix::manifest, spk));

        sign(st, HashPrefix::manifest, type,
            invalidSig ? randomSecretKey() : sk, sfMasterSignature);
        BEAST_EXPECT(invalidSig ^ verify(
            st, HashPrefix::manifest, pk, sfMasterSignature));

        Serializer s;
        st.add(s);

        std::string const m (static_cast<char const*> (s.data()), s.size());
        if (auto r = Manifest::make_Manifest (std::move (m)))
            return std::move (*r);
        Throw<std::runtime_error> ("Could not create a manifest");
        return *Manifest::make_Manifest(std::move(m)); // Silence compiler warning.
    }

    std::string
    makeRevocation
        (SecretKey const& sk, KeyType type, bool invalidSig = false)
    {
        auto const pk = derivePublicKey(type, sk);

        STObject st(sfGeneric);
        st[sfSequence] = std::numeric_limits<std::uint32_t>::max ();
        st[sfPublicKey] = pk;

        sign(st, HashPrefix::manifest, type,
            invalidSig ? randomSecretKey() : sk, sfMasterSignature);
        BEAST_EXPECT(invalidSig ^ verify(
            st, HashPrefix::manifest, pk, sfMasterSignature));

        Serializer s;
        st.add(s);

        return beast::detail::base64_encode (std::string(
            static_cast<char const*> (s.data()), s.size()));
    }

    Manifest
    clone (Manifest const& m)
    {
        return Manifest (m.serialized, m.masterKey, m.signingKey, m.sequence);
    }

    void testLoadStore (ManifestCache& m)
    {
        testcase ("load/store");

        std::string const dbName("ManifestCacheTestDB");
        {
            DatabaseCon::Setup setup;
            setup.dataDir = getDatabasePath ();
            DatabaseCon dbCon(setup, dbName, WalletDBInit, WalletDBCount);

            auto getPopulatedManifests =
                    [](ManifestCache const& cache) -> std::vector<Manifest const*>
                    {
                        std::vector<Manifest const*> result;
                        result.reserve (32);
                        cache.for_each_manifest (
                            [&result](Manifest const& m)
            {result.push_back (&m);});
                        return result;
                    };
            auto sort =
                    [](std::vector<Manifest const*> mv) -> std::vector<Manifest const*>
                    {
                        std::sort (mv.begin (),
                                   mv.end (),
                                   [](Manifest const* lhs, Manifest const* rhs)
            {return lhs->serialized < rhs->serialized;});
                        return mv;
                    };
            std::vector<Manifest const*> const inManifests (
                sort (getPopulatedManifests (m)));

            beast::Journal journal;
            jtx::Env env (*this);
            auto unl = std::make_unique<ValidatorList> (
                m, m, env.timeKeeper(), journal);

            {
                // save should not store untrusted master keys to db
                // except for revocations
                m.save (dbCon, "ValidatorManifests",
                    [&unl](PublicKey const& pubKey)
                    {
                        return unl->listed (pubKey);
                    });

                ManifestCache loaded;

                loaded.load (dbCon, "ValidatorManifests");

                // check that all loaded manifests are revocations
                std::vector<Manifest const*> const loadedManifests (
                    sort (getPopulatedManifests (loaded)));

                for (auto const& man : loadedManifests)
                    BEAST_EXPECT(man->revoked());
            }
            {
                // save should store all trusted master keys to db
                PublicKey emptyLocalKey;
                std::vector<std::string> s1;
                std::vector<std::string> keys;
                std::string cfgManifest;
                for (auto const& man : inManifests)
                    s1.push_back (toBase58(
                        TokenType::TOKEN_NODE_PUBLIC, man->masterKey));
                unl->load (emptyLocalKey, s1, keys);

                m.save (dbCon, "ValidatorManifests",
                    [&unl](PublicKey const& pubKey)
                    {
                        return unl->listed (pubKey);
                    });
                ManifestCache loaded;
                loaded.load (dbCon, "ValidatorManifests");

                // check that the manifest caches are the same
                std::vector<Manifest const*> const loadedManifests (
                    sort (getPopulatedManifests (loaded)));

                if (inManifests.size () == loadedManifests.size ())
                {
                    BEAST_EXPECT(std::equal
                            (inManifests.begin (), inManifests.end (),
                             loadedManifests.begin (),
                             [](Manifest const* lhs, Manifest const* rhs)
                             {return *lhs == *rhs;}));
                }
                else
                {
                    fail ();
                }
            }
            {
                // load config manifest
                ManifestCache loaded;
                std::vector<std::string> const emptyRevocation;

                std::string const badManifest = "bad manifest";
                BEAST_EXPECT(! loaded.load (
                    dbCon, "ValidatorManifests", badManifest, emptyRevocation));

                auto const sk  = randomSecretKey();
                auto const pk  = derivePublicKey(KeyType::ed25519, sk);
                auto const kp = randomKeyPair(KeyType::secp256k1);

                std::string const cfgManifest =
                    makeManifestString (pk, sk, kp.first, kp.second, 0);

                BEAST_EXPECT(loaded.load (
                    dbCon, "ValidatorManifests", cfgManifest, emptyRevocation));
            }
            {
                // load config revocation
                ManifestCache loaded;
                std::string const emptyManifest;

                std::vector<std::string> const badRevocation = { "bad revocation" };
                BEAST_EXPECT(! loaded.load (
                    dbCon, "ValidatorManifests", emptyManifest, badRevocation));

                auto const sk  = randomSecretKey();
                auto const keyType = KeyType::ed25519;
                auto const pk  = derivePublicKey(keyType, sk);
                auto const kp = randomKeyPair(KeyType::secp256k1);
                std::vector<std::string> const nonRevocation =
                    { makeManifestString (pk, sk, kp.first, kp.second, 0) };

                BEAST_EXPECT(! loaded.load (
                    dbCon, "ValidatorManifests", emptyManifest, nonRevocation));
                BEAST_EXPECT(! loaded.revoked(pk));

                std::vector<std::string> const badSigRevocation =
                    { makeRevocation (sk, keyType, true /* invalidSig */) };
                BEAST_EXPECT(! loaded.load (
                    dbCon, "ValidatorManifests", emptyManifest, badSigRevocation));
                BEAST_EXPECT(! loaded.revoked(pk));

                std::vector<std::string> const cfgRevocation =
                    { makeRevocation (sk, keyType) };
                BEAST_EXPECT(loaded.load (
                    dbCon, "ValidatorManifests", emptyManifest, cfgRevocation));

                BEAST_EXPECT(loaded.revoked(pk));
            }
        }
        boost::filesystem::remove (getDatabasePath () /
                                   boost::filesystem::path (dbName));
    }

    void testGetSignature()
    {
        testcase ("getSignature");
        auto const sk = randomSecretKey();
        auto const pk = derivePublicKey(KeyType::ed25519, sk);
        auto const kp = randomKeyPair(KeyType::secp256k1);
        auto const m = make_Manifest (
            sk, KeyType::ed25519, kp.second, KeyType::secp256k1, 0);

        STObject st(sfGeneric);
        st[sfSequence] = 0;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = kp.first;
        Serializer ss;
        ss.add32(HashPrefix::manifest);
        st.addWithoutSigningFields(ss);
        auto const sig = sign(KeyType::secp256k1, kp.second, ss.slice());
        BEAST_EXPECT(strHex(sig) == strHex(m.getSignature()));

        auto const masterSig = sign(KeyType::ed25519, sk, ss.slice());
        BEAST_EXPECT(strHex(masterSig) == strHex(m.getMasterSignature()));
    }

    void testGetKeys()
    {
        testcase ("getKeys");

        ManifestCache cache;
        auto const sk  = randomSecretKey();
        auto const pk  = derivePublicKey(KeyType::ed25519, sk);

        // getSigningKey should return same key if there is no manifest
        BEAST_EXPECT(cache.getSigningKey(pk) == pk);

        // getSigningKey should return the ephemeral public key
        // for the listed validator master public key
        // getMasterKey should return the listed validator master key
        // for that ephemeral public key
        auto const kp0 = randomKeyPair(KeyType::secp256k1);
        auto const m0  = make_Manifest (
            sk, KeyType::ed25519, kp0.second, KeyType::secp256k1, 0);
        BEAST_EXPECT(cache.applyManifest(clone (m0)) ==
                ManifestDisposition::accepted);
        BEAST_EXPECT(cache.getSigningKey(pk) == kp0.first);
        BEAST_EXPECT(cache.getMasterKey(kp0.first) == pk);

        // getSigningKey should return the latest ephemeral public key
        // for the listed validator master public key
        // getMasterKey should only return a master key for the latest
        // ephemeral public key
        auto const kp1 = randomKeyPair(KeyType::secp256k1);
        auto const m1  = make_Manifest (
            sk, KeyType::ed25519, kp1.second, KeyType::secp256k1, 1);
        BEAST_EXPECT(cache.applyManifest(clone (m1)) ==
                ManifestDisposition::accepted);
        BEAST_EXPECT(cache.getSigningKey(pk) == kp1.first);
        BEAST_EXPECT(cache.getMasterKey(kp1.first) == pk);
        BEAST_EXPECT(cache.getMasterKey(kp0.first) == kp0.first);

        // getSigningKey and getMasterKey should return the same keys if
        // a new manifest is applied with the same signing key but a higher
        // sequence
        auto const m2  = make_Manifest (
            sk, KeyType::ed25519, kp1.second, KeyType::secp256k1, 2);
        BEAST_EXPECT(cache.applyManifest(clone (m2)) ==
                ManifestDisposition::accepted);
        BEAST_EXPECT(cache.getSigningKey(pk) == kp1.first);
        BEAST_EXPECT(cache.getMasterKey(kp1.first) == pk);
        BEAST_EXPECT(cache.getMasterKey(kp0.first) == kp0.first);

        // getSigningKey should return boost::none for a
        // revoked master public key
        // getMasterKey should return boost::none for an ephemeral public key
        // from a revoked master public key
        auto const kpMax = randomKeyPair(KeyType::secp256k1);
        auto const mMax = make_Manifest (
            sk, KeyType::ed25519, kpMax.second, KeyType::secp256k1,
            std::numeric_limits<std::uint32_t>::max ());
        BEAST_EXPECT(cache.applyManifest(clone (mMax)) ==
                ManifestDisposition::accepted);
        BEAST_EXPECT(cache.revoked(pk));
        BEAST_EXPECT(cache.getSigningKey(pk) == pk);
        BEAST_EXPECT(cache.getMasterKey(kpMax.first) == kpMax.first);
        BEAST_EXPECT(cache.getMasterKey(kp1.first) == kp1.first);
    }

    void testValidatorToken()
    {
        testcase ("validator token");

        {
            auto const valSecret = parseBase58<SecretKey>(
                TokenType::TOKEN_NODE_PRIVATE,
                "paQmjZ37pKKPMrgadBLsuf9ab7Y7EUNzh27LQrZqoexpAs31nJi");

            // Format token string to test trim()
            std::vector<std::string> const tokenBlob = {
                "    eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT\n",
                " \tQzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlkYWY2IiwibWFuaWZl     \n",
                "\tc3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZDeE\n",
                "\t hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG\t  \t\n",
                "bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2\n",
                "hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1\n",
                "NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj\n",
                "VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==\n"
            };

            auto const manifest =
                "JAAAAAFxIe1FtwmimvGtH2iCcMJqC9gVFKilGfw1/vCxHXXLplc2GnMhAkE1agqXxBwD"
                "wDbID6OMSYuM0FDAlpAgNk8SKFn7MO2fdkcwRQIhAOngu9sAKqXYouJ+l2V0W+sAOkVB"
                "+ZRS6PShlJAfUsXfAiBsVJGesaadOJc/aAZokS1vymGmVrlHPKWX3Yywu6in8HASQKPu"
                "gBD67kMaRFGvmpATHlGKJdvDFlWPYy5AqDedFv5TJa2w0i21eq3MYywLVJZnFOr7C0kw"
                "2AiTzSCjIzditQ8=";

            auto const token = ValidatorToken::make_ValidatorToken(tokenBlob);
            BEAST_EXPECT(token);
            BEAST_EXPECT(token->validationSecret == *valSecret);
            BEAST_EXPECT(token->manifest == manifest);
        }
        {
            std::vector<std::string> const badToken = { "bad token" };
            BEAST_EXPECT(! ValidatorToken::make_ValidatorToken(badToken));
        }
    }

    void
    run() override
    {
        ManifestCache cache;
        {
            testcase ("apply");
            auto const accepted = ManifestDisposition::accepted;
            auto const stale = ManifestDisposition::stale;
            auto const invalid = ManifestDisposition::invalid;

            auto const sk_a = randomSecretKey();
            auto const pk_a = derivePublicKey(KeyType::ed25519, sk_a);
            auto const kp_a = randomKeyPair(KeyType::secp256k1);
            auto const s_a0 = make_Manifest (
                sk_a, KeyType::ed25519, kp_a.second, KeyType::secp256k1, 0);
            auto const s_a1 = make_Manifest (
                sk_a, KeyType::ed25519, kp_a.second, KeyType::secp256k1, 1);
            auto const s_aMax = make_Manifest (
                sk_a, KeyType::ed25519, kp_a.second, KeyType::secp256k1,
                std::numeric_limits<std::uint32_t>::max ());

            auto const sk_b = randomSecretKey();
            auto const kp_b = randomKeyPair(KeyType::secp256k1);
            auto const s_b0 = make_Manifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 0);
            auto const s_b1 = make_Manifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 1);
            auto const s_b2 = make_Manifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 2,
                true);  // invalidSig
            auto const fake = s_b1.serialized + '\0';

            // applyManifest should accept new manifests with
            // higher sequence numbers
            BEAST_EXPECT(cache.applyManifest (clone (s_a0)) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0)) == stale);

            BEAST_EXPECT(cache.applyManifest (clone (s_a1)) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_a1)) == stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0)) == stale);

            // applyManifest should accept manifests with max sequence numbers
            // that revoke the master public key
            BEAST_EXPECT(!cache.revoked (pk_a));
            BEAST_EXPECT(s_aMax.revoked ());
            BEAST_EXPECT(cache.applyManifest (clone (s_aMax)) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_aMax)) == stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a1)) == stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0)) == stale);
            BEAST_EXPECT(cache.revoked (pk_a));

            // applyManifest should reject manifests with invalid signatures
            BEAST_EXPECT(cache.applyManifest (clone (s_b0)) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_b0)) == stale);

            BEAST_EXPECT(!Manifest::make_Manifest(fake));
            BEAST_EXPECT(cache.applyManifest (clone (s_b2)) == invalid);
        }
        testLoadStore (cache);
        testGetSignature ();
        testGetKeys ();
        testValidatorToken ();
    }
};

BEAST_DEFINE_TESTSUITE(Manifest,app,ripple);

} // test
} // ripple
