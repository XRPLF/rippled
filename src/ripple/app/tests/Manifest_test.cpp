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
#include <ripple/basics/TestSuite.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STExchange.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/utility/in_place_factory.hpp>

namespace ripple {
namespace tests {

class Manifest_test : public ripple::TestSuite
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

    class TestThread
    {
    private:
        boost::asio::io_service io_service_;
        boost::optional<boost::asio::io_service::work> work_;
        std::thread thread_;

    public:
        TestThread()
            : work_(boost::in_place(std::ref(io_service_)))
            , thread_([&]() { this->io_service_.run(); })
        {
        }

        ~TestThread()
        {
            work_ = boost::none;
            thread_.join();
        }

        boost::asio::io_service&
        get_io_service()
        {
            return io_service_;
        }
    };

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

    Manifest
    make_Manifest
        (KeyType type, SecretKey const& sk, PublicKey const& spk, int seq,
         bool broken = false)
    {
        auto const pk = derivePublicKey(type, sk);

        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;

        sign(st, HashPrefix::manifest, type, sk);
        BEAST_EXPECT(verify(st, HashPrefix::manifest, pk, true));

        if (broken)
        {
            set(st, sfSequence, seq + 1);
        }

        Serializer s;
        st.add(s);

        std::string const m (static_cast<char const*> (s.data()), s.size());
        if (auto r = ripple::make_Manifest (std::move (m)))
            return std::move (*r);
        Throw<std::runtime_error> ("Could not create a manifest");
        return *ripple::make_Manifest(std::move(m)); // Silence compiler warning.
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
            // create a database, save the manifest to the db and reload and
            // check that the manifest caches are the same
            DatabaseCon::Setup setup;
            setup.dataDir = getDatabasePath ();
            DatabaseCon dbCon(setup, dbName, WalletDBInit, WalletDBCount);

            m.save (dbCon);

            TestThread thread;
            beast::Journal journal;
            auto unl = std::make_unique<ValidatorList> (
                m, thread.get_io_service (), journal);

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
            {
                // load should not load untrusted master keys from db
                ManifestCache loaded (journal);

                loaded.load (dbCon, *unl);
                for (auto const& man : inManifests)
                    BEAST_EXPECT(! loaded.getSigningKey (man->masterKey));
            }
            {
                // load should load all trusted master keys from db
                ManifestCache loaded (journal);

                PublicKey emptyLocalKey;
                std::vector<std::string> s1;
                std::vector<std::string> sites;
                std::vector<std::string> keys;
                std::vector<std::string> cfgManifest;
                for (auto const& man : inManifests)
                    s1.push_back (toBase58(
                        TokenType::TOKEN_NODE_PUBLIC, man->masterKey));
                unl->load (emptyLocalKey, s1, sites, keys, cfgManifest);

                loaded.load (dbCon, *unl);

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
        auto const m = make_Manifest (KeyType::ed25519, sk, kp.first, 0);

        STObject st(sfGeneric);
        st[sfSequence] = 0;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = kp.first;
        Serializer ss;
        ss.add32(HashPrefix::manifest);
        st.addWithoutSigningFields(ss);
        auto const sig = sign(KeyType::ed25519, sk, ss.slice());

        BEAST_EXPECT(strHex(sig) == strHex(m.getSignature()));
    }

    void testGetKeys()
    {
        testcase ("getKeys");

        beast::Journal journal;
        ManifestCache cache (journal);
        auto const sk  = randomSecretKey();
        auto const pk  = derivePublicKey(KeyType::ed25519, sk);

        TestThread thread;
        auto unl = std::make_unique<ValidatorList> (
            cache, thread.get_io_service (), beast::Journal ());
        PublicKey emptyLocalKey;
        std::vector<std::string> cfgManifest;
        std::vector<std::string> validators;
        std::vector<std::string> validatorSites;
        std::vector<std::string> listKeys;
        validators.push_back (toBase58(
            TokenType::TOKEN_NODE_PUBLIC,
            pk));

        // getSigningKey should return boost::none for an
        // unknown master public key
        BEAST_EXPECT(!unl->listed(pk));
        unl->load (
            emptyLocalKey, validators, validatorSites, listKeys, cfgManifest);
        BEAST_EXPECT(unl->listed(pk));
        BEAST_EXPECT(!cache.getSigningKey(pk));

        // getSigningKey should return the ephemeral public key
        // for the listed validator master public key
        // getMasterKey should return the listed validator master key
        // for that ephemeral public key
        auto const kp0 = randomKeyPair(KeyType::secp256k1);
        auto const m0  = make_Manifest (
            KeyType::ed25519, sk, kp0.first, 0);
        BEAST_EXPECT(cache.applyManifest(clone (m0), *unl) ==
                ManifestDisposition::accepted);
        BEAST_EXPECT(cache.getSigningKey(pk) == kp0.first);
        BEAST_EXPECT(cache.getMasterKey(kp0.first) == pk);

        // getSigningKey should return the latest ephemeral public key
        // for the listed validator master public key
        // getMasterKey should only return a master key for the latest
        // ephemeral public key
        auto const kp1 = randomKeyPair(KeyType::secp256k1);
        auto const m1  = make_Manifest (
            KeyType::ed25519, sk, kp1.first, 1);
        BEAST_EXPECT(cache.applyManifest(clone (m1), *unl) ==
                ManifestDisposition::accepted);
        BEAST_EXPECT(cache.getSigningKey(pk) == kp1.first);
        BEAST_EXPECT(cache.getMasterKey(kp1.first) == pk);
        BEAST_EXPECT(! cache.getMasterKey(kp0.first));

        // getSigningKey and getMasterKey should return the same keys if
        // a new manifest is applied with the same signing key but a higher
        // sequence
        auto const m2  = make_Manifest (
            KeyType::ed25519, sk, kp1.first, 2);
        BEAST_EXPECT(cache.applyManifest(clone (m2), *unl) ==
                ManifestDisposition::accepted);
        BEAST_EXPECT(cache.getSigningKey(pk) == kp1.first);
        BEAST_EXPECT(cache.getMasterKey(kp1.first) == pk);
        BEAST_EXPECT(! cache.getMasterKey(kp0.first));

        // getSigningKey should return boost::none for a
        // revoked master public key
        // getMasterKey should return boost::none for an ephemeral public key
        // from a revoked master public key
        auto const kpMax = randomKeyPair(KeyType::secp256k1);
        auto const mMax = make_Manifest (
            KeyType::ed25519, sk, kpMax.first,
            std::numeric_limits<std::uint32_t>::max ());
        BEAST_EXPECT(cache.applyManifest(clone (mMax), *unl) ==
                ManifestDisposition::accepted);
        BEAST_EXPECT(cache.revoked(pk));
        BEAST_EXPECT(! cache.getSigningKey(pk));
        BEAST_EXPECT(! cache.getMasterKey(kpMax.first));
        BEAST_EXPECT(! cache.getMasterKey(kp1.first));
    }

    void
    run() override
    {
        beast::Journal journal;
        ManifestCache cache (journal);
        {
            testcase ("apply");
            auto const accepted = ManifestDisposition::accepted;
            auto const untrusted = ManifestDisposition::untrusted;
            auto const stale = ManifestDisposition::stale;
            auto const invalid = ManifestDisposition::invalid;

            auto const sk_a = randomSecretKey();
            auto const pk_a = derivePublicKey(KeyType::ed25519, sk_a);
            auto const kp_a = randomKeyPair(KeyType::secp256k1);
            auto const s_a0 = make_Manifest (
                KeyType::ed25519, sk_a, kp_a.first, 0);
            auto const s_a1 = make_Manifest (
                KeyType::ed25519, sk_a, kp_a.first, 1);
            auto const s_aMax = make_Manifest (
                KeyType::ed25519, sk_a, kp_a.first,
                std::numeric_limits<std::uint32_t>::max ());

            auto const sk_b = randomSecretKey();
            auto const pk_b = derivePublicKey(KeyType::ed25519, sk_b);
            auto const kp_b = randomKeyPair(KeyType::secp256k1);
            auto const s_b0 = make_Manifest (
                KeyType::ed25519, sk_b, kp_b.first, 0);
            auto const s_b1 = make_Manifest (
                KeyType::ed25519, sk_b, kp_b.first, 1);
            auto const s_b2 =
                make_Manifest (KeyType::ed25519, sk_b, kp_b.first, 2, true);  // broken
            auto const fake = s_b1.serialized + '\0';

            TestThread thread;
            auto unl = std::make_unique<ValidatorList> (
                cache, thread.get_io_service (), journal);

            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl) == untrusted);

            PublicKey emptyLocalKey;
            std::vector<std::string> s;
            std::vector<std::string> sites;
            std::vector<std::string> keys;
            std::vector<std::string> cfgManifest;
            s.push_back (toBase58(
                TokenType::TOKEN_NODE_PUBLIC, pk_a));
            s.push_back (toBase58(
                TokenType::TOKEN_NODE_PUBLIC, pk_b));
            unl->load (emptyLocalKey, s, sites, keys, cfgManifest);

            // applyManifest should accept new manifests with
            // higher sequence numbers
            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl) == stale);

            BEAST_EXPECT(cache.applyManifest (clone (s_a1), *unl) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_a1), *unl) == stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl) == stale);

            // applyManifest should accept manifests with max sequence numbers
            // that revoke the master public key
            BEAST_EXPECT(!cache.revoked (pk_a));
            BEAST_EXPECT(s_aMax.revoked ());
            BEAST_EXPECT(cache.applyManifest (clone (s_aMax), *unl) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_aMax), *unl) == stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a1), *unl) == stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl) == stale);
            BEAST_EXPECT(cache.revoked (pk_a));

            // applyManifest should reject manifests with invalid signatures
            BEAST_EXPECT(cache.applyManifest (clone (s_b0), *unl) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_b0), *unl) == stale);

            BEAST_EXPECT(!ripple::make_Manifest(fake));
            BEAST_EXPECT(cache.applyManifest (clone (s_b2), *unl) == invalid);
        }
        testLoadStore (cache);
        testGetSignature ();
        testGetKeys ();
    }
};

BEAST_DEFINE_TESTSUITE(Manifest,app,ripple);

} // tests
} // ripple
