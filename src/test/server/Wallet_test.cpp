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

#include <soci/session.h>

#include <filesystem>
#include <string>

namespace xrpl::test {

class Wallet_test : public beast::unit_test::Suite
{
    static std::filesystem::path
    dbDir()
    {
        return std::filesystem::current_path() / "wallet_test_dbs";
    }

    static void
    ensureDir()
    {
        using namespace std::filesystem;
        if (!exists(dbDir()))
            create_directory(dbDir());
    }

    static void
    insertIdentity(soci::session& session, std::string const& pubB58, std::string const& secB58)
    {
        session << "DELETE FROM NodeIdentity;";
        session << "INSERT INTO NodeIdentity (PublicKey,PrivateKey) VALUES('" << pubB58 << "','"
                << secB58 << "');";
    }

    // getNodeIdentity() returns a stored secp256k1 or Dilithium keypair unchanged, whatever type
    // it is asked to generate, so an upgrade never rotates a node's identity.
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
        auto const [returnedPk, returnedSk] = getNodeIdentity(*session, KeyType::Dilithium);

        BEAST_EXPECT(returnedPk == pk);
        BEAST_EXPECT(returnedSk.toString() == sk.toString());
        BEAST_EXPECT(publicKeyType(returnedPk.slice()) == kt);

        // Tidy up so successive runs / keytypes don't collide.
        std::filesystem::remove(dbDir() / dbName);
    }

    // When no identity is stored, a fresh identity of the requested type is generated and
    // persisted.
    void
    testGeneratesRequestedTypeWhenAbsent(KeyType kt, std::string const& label)
    {
        testcase("generates_" + label + "_for_new_node");

        ensureDir();
        std::string const dbName = "wallet_new_" + label + ".sqlite";

        jtx::Env env(*this);
        DatabaseCon::Setup setup;
        setup.dataDir = dbDir();
        auto dbCon = makeTestWalletDB(setup, dbName, env.journal);

        auto session = dbCon->checkoutDb();
        auto const [pk1, sk1] = getNodeIdentity(*session, kt);
        BEAST_EXPECT(publicKeyType(pk1.slice()) == kt);

        // A second call returns the same keypair, whatever type it asks for.
        auto const [pk2, sk2] = getNodeIdentity(
            *session, kt == KeyType::Secp256k1 ? KeyType::Dilithium : KeyType::Secp256k1);
        BEAST_EXPECT(pk1 == pk2);
        BEAST_EXPECT(sk1.toString() == sk2.toString());

        std::filesystem::remove(dbDir() / dbName);
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
        auto const [pk, sk] = getNodeIdentity(*session, KeyType::Secp256k1);

        // Returned pair must be self-consistent, regardless of keytype.
        auto const kt = publicKeyType(pk.slice());
        BEAST_EXPECT(kt.has_value());
        if (kt)
            BEAST_EXPECT(pk == derivePublicKey(*kt, sk));

        // And must not be the corrupted stored pair.
        BEAST_EXPECT(pk != pkMismatched);

        std::filesystem::remove(dbDir() / dbName);
    }

    // An Ed25519 identity (never supported on the wire) is not returned even if it is stored;
    // getNodeIdentity generates a new identity of the requested type instead.
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
        auto const [returnedPk, returnedSk] = getNodeIdentity(*session, KeyType::Secp256k1);

        BEAST_EXPECT(returnedPk != pk);
        BEAST_EXPECT(publicKeyType(returnedPk.slice()) == KeyType::Secp256k1);

        std::filesystem::remove(dbDir() / dbName);
    }

    void
    run() override
    {
        testPreservesLegacyIdentity(KeyType::Secp256k1, "secp256k1");
        testPreservesLegacyIdentity(KeyType::Dilithium, "dilithium");
        testGeneratesRequestedTypeWhenAbsent(KeyType::Secp256k1, "secp256k1");
        testGeneratesRequestedTypeWhenAbsent(KeyType::Dilithium, "dilithium");
        testRecoversFromCorruptedRow();
        testRejectsEd25519Identity();
    }
};

BEAST_DEFINE_TESTSUITE(Wallet, server, xrpl);

}  // namespace xrpl::test
