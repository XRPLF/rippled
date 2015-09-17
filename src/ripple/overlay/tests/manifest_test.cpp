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
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STExchange.h>
#include <ripple/test/jtx.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace ripple {
namespace tests {

class manifest_test : public ripple::TestSuite
{
private:
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
            throw std::runtime_error ("Cannot create directory: " +
                                      dbPath.string ());
        }
    }
    static boost::filesystem::path getDatabasePath ()
    {
        return boost::filesystem::current_path () / "manifest_test_databases";
    }
public:
    manifest_test ()
    {
        try
        {
            setupDatabaseDir (getDatabasePath ());
        }
        catch (...)
        {
        }
    }
    ~manifest_test ()
    {
        try
        {
            cleanupDatabaseDir (getDatabasePath ());
        }
        catch (...)
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
        set(st, sfSequence, seq);
        set(st, sfPublicKey, pk);
        set(st, sfSigningPubKey, spk);

        sign(st, HashPrefix::manifest, type, sk);
        expect(verify(st, HashPrefix::manifest, pk, true));

        if (broken)
        {
            set(st, sfSequence, seq + 1);
        }

        Serializer s;
        st.add(s);

        std::string const m (static_cast<char const*> (s.data()), s.size());
        if (auto r = ripple::make_Manifest (std::move (m)))
        {
            return std::move (*r);
        }
        throw std::runtime_error("Could not create a manifest");
    }

    Manifest
    clone (Manifest const& m)
    {
        return Manifest (m.serialized, m.masterKey, m.signingKey, m.sequence);
    }

    void testLoadStore (ManifestCache const& m, UniqueNodeList& unl)
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

            ManifestCache loaded;
            beast::Journal journal;
            loaded.load (dbCon, unl, journal);

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
            std::vector<Manifest const*> const loadedManifests (
                sort (getPopulatedManifests (loaded)));
            if (inManifests.size () == loadedManifests.size ())
            {
                expect (std::equal
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
        boost::filesystem::remove (getDatabasePath () /
                                   boost::filesystem::path (dbName));
    }

    void
    run() override
    {
        ManifestCache cache;
        test::jtx::Env env(*this);
        auto& unl = env.app().getUNL();
        {
            testcase ("apply");
            auto const accepted = ManifestDisposition::accepted;
            auto const untrusted = ManifestDisposition::untrusted;
            auto const stale = ManifestDisposition::stale;
            auto const invalid = ManifestDisposition::invalid;

            beast::Journal journal;

            auto const sk_a = randomSecretKey();
            auto const pk_a = derivePublicKey(KeyType::ed25519, sk_a);
            auto const kp_a = randomKeyPair(KeyType::secp256k1);
            auto const s_a0 = make_Manifest (KeyType::ed25519, sk_a, kp_a.first, 0);
            auto const s_a1 = make_Manifest (KeyType::ed25519, sk_a, kp_a.first, 1);

            auto const sk_b = randomSecretKey();
            auto const pk_b = derivePublicKey(KeyType::ed25519, sk_b);
            auto const kp_b = randomKeyPair(KeyType::secp256k1);
            auto const s_b0 = make_Manifest (KeyType::ed25519, sk_b, kp_b.first, 0);
            auto const s_b1 = make_Manifest (KeyType::ed25519, sk_b, kp_b.first, 1);
            auto const s_b2 =
                make_Manifest (KeyType::ed25519, sk_b, kp_b.first, 2, true);  // broken
            auto const fake = s_b1.serialized + '\0';

            expect (cache.applyManifest (clone (s_a0), unl, journal) == untrusted,
                    "have to install a trusted key first");

            cache.addTrustedKey (pk_a, "a");
            cache.addTrustedKey (pk_b, "b");

            expect (cache.applyManifest (clone (s_a0), unl, journal) == accepted);
            expect (cache.applyManifest (clone (s_a0), unl, journal) == stale);

            expect (cache.applyManifest (clone (s_a1), unl, journal) == accepted);
            expect (cache.applyManifest (clone (s_a1), unl, journal) == stale);
            expect (cache.applyManifest (clone (s_a0), unl, journal) == stale);

            expect (cache.applyManifest (clone (s_b0), unl, journal) == accepted);
            expect (cache.applyManifest (clone (s_b0), unl, journal) == stale);

            expect (!ripple::make_Manifest(fake));
            expect (cache.applyManifest (clone (s_b2), unl, journal) == invalid);
        }
        testLoadStore (cache, unl);
    }
};

BEAST_DEFINE_TESTSUITE(manifest,overlay,ripple);

} // tests
} // ripple
