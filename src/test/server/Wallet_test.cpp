#include <test/jtx/Env.h>
#include <test/unit_test/utils.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/server/Wallet.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <soci/session.h>

#include <string>

namespace xrpl::test {

class Wallet_test : public beast::unit_test::Suite
{
    static boost::filesystem::path
    dbDir()
    {
        return boost::filesystem::current_path() / "wallet_test_dbs";
    }

    static void
    ensureDir()
    {
        using namespace boost::filesystem;
        if (!exists(dbDir()))
            create_directory(dbDir());
    }

    static void
    insertIdentity(
        soci::session& session,
        std::string const& pubB58,
        std::string const& secB58)
    {
        session << "DELETE FROM NodeIdentity;";
        session << "INSERT INTO NodeIdentity (PublicKey,PrivateKey) VALUES('"
                << pubB58 << "','" << secB58 << "');";
    }

    // Regression: getNodeIdentity() must return the stored keypair
    // unchanged when the stored pubkey was generated with a legacy keytype
    // (secp256k1 or ed25519). A prior version of this function hard-coded
    // KeyType::Dilithium when re-deriving the pubkey to compare against the
    // stored one, which silently rotated every upgrading operator's node
    // identity — orphaning peer reservations and any UNL pin on that node.
    void
    testPreservesLegacyIdentity(KeyType kt, std::string const& label)
    {
        testcase("preserves_" + label + "_identity_across_upgrade");

        ensureDir();
        std::string const dbName = "wallet_" + label + ".sqlite";

        jtx::Env env(*this);
        DatabaseCon::Setup setup;
        setup.dataDir = dbDir();
        auto dbCon = makeTestWalletDB(setup, dbName, env.journal);

        auto const seed = randomSeed();
        auto const sk = generateSecretKey(kt, seed);
        auto const pk = derivePublicKey(kt, sk);
        auto const pkB58 = toBase58(TokenType::NodePublic, pk);
        auto const skB58 = toBase58(TokenType::NodePrivate, sk);

        {
            auto session = dbCon->checkoutDb();
            insertIdentity(*session, pkB58, skB58);
        }

        auto session = dbCon->checkoutDb();
        auto const [returnedPk, returnedSk] = getNodeIdentity(*session);

        BEAST_EXPECT(returnedPk == pk);
        BEAST_EXPECT(returnedSk.toString() == sk.toString());
        BEAST_EXPECT(publicKeyType(returnedPk.slice()) == kt);

        // Tidy up so successive runs / keytypes don't collide.
        boost::filesystem::remove(dbDir() / dbName);
    }

    // When no identity is stored, a fresh dilithium identity is generated
    // and persisted.
    void
    testGeneratesDilithiumWhenAbsent()
    {
        testcase("generates_dilithium_for_new_node");

        ensureDir();
        std::string const dbName = "wallet_new.sqlite";

        jtx::Env env(*this);
        DatabaseCon::Setup setup;
        setup.dataDir = dbDir();
        auto dbCon = makeTestWalletDB(setup, dbName, env.journal);

        auto session = dbCon->checkoutDb();
        auto const [pk1, sk1] = getNodeIdentity(*session);
        BEAST_EXPECT(publicKeyType(pk1.slice()) == KeyType::Dilithium);

        // A second call must return the same keypair (persistence).
        auto const [pk2, sk2] = getNodeIdentity(*session);
        BEAST_EXPECT(pk1 == pk2);
        BEAST_EXPECT(sk1.toString() == sk2.toString());

        boost::filesystem::remove(dbDir() / dbName);
    }

    // If the stored pubkey doesn't match the stored secret (corrupted row),
    // fall through and generate a new identity rather than returning a
    // mismatched pair.
    void
    testRecoversFromCorruptedRow()
    {
        testcase("recovers_from_corrupted_row");

        ensureDir();
        std::string const dbName = "wallet_corrupt.sqlite";

        jtx::Env env(*this);
        DatabaseCon::Setup setup;
        setup.dataDir = dbDir();
        auto dbCon = makeTestWalletDB(setup, dbName, env.journal);

        auto const sk1 = generateSecretKey(KeyType::Secp256k1, randomSeed());
        auto const sk2 = generateSecretKey(KeyType::Secp256k1, randomSeed());
        auto const pkMismatched = derivePublicKey(KeyType::Secp256k1, sk2);

        {
            auto session = dbCon->checkoutDb();
            insertIdentity(
                *session,
                toBase58(TokenType::NodePublic, pkMismatched),
                toBase58(TokenType::NodePrivate, sk1));
        }

        auto session = dbCon->checkoutDb();
        auto const [pk, sk] = getNodeIdentity(*session);

        // Returned pair must be self-consistent, regardless of keytype.
        auto const kt = publicKeyType(pk.slice());
        BEAST_EXPECT(kt.has_value());
        if (kt)
            BEAST_EXPECT(pk == derivePublicKey(*kt, sk));

        // And must not be the corrupted stored pair.
        BEAST_EXPECT(pk != pkMismatched);

        boost::filesystem::remove(dbDir() / dbName);
    }

    // An Ed25519 identity (never supported on the wire) must NOT be
    // returned even if it somehow ends up in the DB; getNodeIdentity must
    // treat it as invalid and generate a new (dilithium) identity instead.
    void
    testRejectsEd25519Identity()
    {
        testcase("rejects_ed25519_node_identity");

        ensureDir();
        std::string const dbName = "wallet_ed25519.sqlite";

        jtx::Env env(*this);
        DatabaseCon::Setup setup;
        setup.dataDir = dbDir();
        auto dbCon = makeTestWalletDB(setup, dbName, env.journal);

        auto const seed = randomSeed();
        auto const sk = generateSecretKey(KeyType::Ed25519, seed);
        auto const pk = derivePublicKey(KeyType::Ed25519, sk);

        {
            auto session = dbCon->checkoutDb();
            insertIdentity(
                *session,
                toBase58(TokenType::NodePublic, pk),
                toBase58(TokenType::NodePrivate, sk));
        }

        auto session = dbCon->checkoutDb();
        auto const [returnedPk, returnedSk] = getNodeIdentity(*session);

        BEAST_EXPECT(returnedPk != pk);
        BEAST_EXPECT(publicKeyType(returnedPk.slice()) == KeyType::Dilithium);

        boost::filesystem::remove(dbDir() / dbName);
    }

    void
    run() override
    {
        testPreservesLegacyIdentity(KeyType::Secp256k1, "secp256k1");
        testGeneratesDilithiumWhenAbsent();
        testRecoversFromCorruptedRow();
        testRejectsEd25519Identity();
    }
};

BEAST_DEFINE_TESTSUITE(Wallet, server, xrpl);

}  // namespace xrpl::test
