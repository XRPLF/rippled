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
#include <ripple/basics/contract.h>
#include <ripple/basics/StringUtilities.h>
#include <test/jtx/TestSuite.h>
#include <ripple/overlay/impl/Manifest.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STExchange.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace ripple {
namespace tests {

class manifest_test : public ripple::TestSuite
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
    manifest_test ()
    {
        try
        {
            setupDatabaseDir (getDatabasePath ());
        }
        catch (std::exception const&)
        {
        }
    }
    ~manifest_test ()
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
        (SecretKey const& sk, KeyType type, SecretKey const& ssk, KeyType stype,
         int seq, bool broken = false)
    {
        auto const pk = derivePublicKey(type, sk);
        auto const spk = derivePublicKey(stype, ssk);

        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;

        sign(st, HashPrefix::manifest, stype, ssk);
        BEAST_EXPECT(verify(st, HashPrefix::manifest, spk));

        sign(st, HashPrefix::manifest, type, sk, sfMasterSignature);
        BEAST_EXPECT(verify(
            st, HashPrefix::manifest, pk, sfMasterSignature));

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


    void
    testConfigLoad ()
    {
        testcase ("Config Load");

        ManifestCache cache;
        beast::Journal journal;

        std::vector<PublicKey> network;
        network.reserve(8);

        while (network.size () != 8)
            network.push_back (randomMasterKey());

        auto format = [](
            PublicKey const &publicKey,
            char const* comment = nullptr)
        {
            auto ret = toBase58(
                TokenType::TOKEN_NODE_PUBLIC,
                publicKey);

            if (comment)
                ret += comment;

            return ret;
        };

        Section s1;

        // Correct (empty) configuration
        BEAST_EXPECT(cache.loadValidatorKeys (s1, journal));
        BEAST_EXPECT(cache.size() == 0);

        // Correct configuration
        s1.append (format (network[0]));
        s1.append (format (network[1], " Comment"));
        s1.append (format (network[2], " Multi Word Comment"));
        s1.append (format (network[3], "    Leading Whitespace"));
        s1.append (format (network[4], " Trailing Whitespace    "));
        s1.append (format (network[5], "    Leading & Trailing Whitespace    "));
        s1.append (format (network[6], "    Leading, Trailing & Internal    Whitespace    "));
        s1.append (format (network[7], "    "));

        BEAST_EXPECT(cache.loadValidatorKeys (s1, journal));

        for (auto const& n : network)
            BEAST_EXPECT(cache.trusted (n));

        // Incorrect configurations:
        Section s2;
        s2.append ("NotAPublicKey");
        BEAST_EXPECT(!cache.loadValidatorKeys (s2, journal));

        Section s3;
        s3.append (format (network[0], "!"));
        BEAST_EXPECT(!cache.loadValidatorKeys (s3, journal));

        Section s4;
        s4.append (format (network[0], "!  Comment"));
        BEAST_EXPECT(!cache.loadValidatorKeys (s4, journal));

        // Check if we properly terminate when we encounter
        // a malformed or unparseable entry:
        auto const masterKey1 = randomMasterKey();
        auto const masterKey2 = randomMasterKey ();

        Section s5;
        s5.append (format (masterKey1, "XXX"));
        s5.append (format (masterKey2));
        BEAST_EXPECT(!cache.loadValidatorKeys (s5, journal));
        BEAST_EXPECT(!cache.trusted (masterKey1));
        BEAST_EXPECT(!cache.trusted (masterKey2));

        // Reject secp256k1 permanent validator keys
        auto const node1 = randomNode ();
        auto const node2 = randomNode ();

        Section s6;
        s6.append (format (node1));
        s6.append (format (node2, " Comment"));
        BEAST_EXPECT(!cache.loadValidatorKeys (s6, journal));
        BEAST_EXPECT(!cache.trusted (node1));
        BEAST_EXPECT(!cache.trusted (node2));

        // Trust our own master public key from configured manifest
        auto unl = std::make_unique<ValidatorList> (journal);

        auto const sk = randomSecretKey();
        auto const kp = randomKeyPair(KeyType::secp256k1);
        auto const m  = make_Manifest (
            sk, KeyType::ed25519, kp.second, KeyType::secp256k1, 0);

        cache.configManifest (clone (m), *unl, journal);
        BEAST_EXPECT(cache.trusted (m.masterKey));
    }

    void testLoadStore (ManifestCache const& m, ValidatorList& unl)
    {
        testcase ("load/store");

        std::string const dbName("ManifestCacheTestDB");
        {
            // create a database, save the manifest to the db and reload and
            // check that the manifest caches are the same
            DatabaseCon::Setup setup;
            setup.dataDir = getDatabasePath ();
            DatabaseCon dbCon(setup, dbName, WalletDBInit, WalletDBCount);

            if (!m.size ())
                fail ();

            m.save (dbCon);

            beast::Journal journal;

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
                ManifestCache loaded;

                loaded.load (dbCon, unl, journal);
                BEAST_EXPECT(loaded.size() == 0);
            }
            {
                // load should load all trusted master keys from db
                ManifestCache loaded;

                for (auto const& man : inManifests)
                    loaded.addTrustedKey (man->masterKey, "");

                loaded.load (dbCon, unl, journal);

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
                // load should remove master key from permanent key list
                ManifestCache loaded;
                auto const iMan = inManifests.begin();

                if (!*iMan)
                    fail ();
                BEAST_EXPECT(m.trusted((*iMan)->masterKey));
                BEAST_EXPECT(unl.insertPermanentKey((*iMan)->masterKey, "trusted key"));
                BEAST_EXPECT(unl.trusted((*iMan)->masterKey));
                loaded.load (dbCon, unl, journal);
                BEAST_EXPECT(!unl.trusted((*iMan)->masterKey));
                BEAST_EXPECT(loaded.trusted((*iMan)->masterKey));
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

    void
    run() override
    {
        ManifestCache cache;
        beast::Journal journal;
        auto unl = std::make_unique<ValidatorList> (journal);
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
                sk_a, KeyType::ed25519, kp_a.second, KeyType::secp256k1, 0);
            auto const s_a1 = make_Manifest (
                sk_a, KeyType::ed25519, kp_a.second, KeyType::secp256k1, 1);

            auto const sk_b = randomSecretKey();
            auto const pk_b = derivePublicKey(KeyType::ed25519, sk_b);
            auto const kp_b = randomKeyPair(KeyType::secp256k1);
            auto const s_b0 = make_Manifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 0);
            auto const s_b1 = make_Manifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 1);
            auto const s_b2 = make_Manifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 2,
                true);  // broken
            auto const fake = s_b1.serialized + '\0';

            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl, journal) == untrusted);

            cache.addTrustedKey (pk_a, "a");
            cache.addTrustedKey (pk_b, "b");

            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl, journal) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl, journal) == stale);

            BEAST_EXPECT(cache.applyManifest (clone (s_a1), *unl, journal) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_a1), *unl, journal) == stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0), *unl, journal) == stale);

            BEAST_EXPECT(cache.applyManifest (clone (s_b0), *unl, journal) == accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_b0), *unl, journal) == stale);

            BEAST_EXPECT(!ripple::make_Manifest(fake));
            BEAST_EXPECT(cache.applyManifest (clone (s_b2), *unl, journal) == invalid);

            // When trusted permanent key is found as manifest master key
            // move to manifest cache
            auto const sk_c = randomSecretKey();
            auto const pk_c = derivePublicKey(KeyType::ed25519, sk_c);
            auto const kp_c = randomKeyPair(KeyType::secp256k1);
            auto const s_c0 = make_Manifest (
                sk_c, KeyType::ed25519, kp_c.second, KeyType::secp256k1, 0);
            BEAST_EXPECT(unl->insertPermanentKey(pk_c, "trusted key"));
            BEAST_EXPECT(unl->trusted(pk_c));
            BEAST_EXPECT(!cache.trusted(pk_c));
            BEAST_EXPECT(cache.applyManifest(clone (s_c0), *unl, journal) == accepted);
            BEAST_EXPECT(!unl->trusted(pk_c));
            BEAST_EXPECT(cache.trusted(pk_c));
        }
        testConfigLoad();
        testLoadStore (cache, *unl);
        testGetSignature ();
    }
};

BEAST_DEFINE_TESTSUITE(manifest,overlay,ripple);

} // tests
} // ripple
