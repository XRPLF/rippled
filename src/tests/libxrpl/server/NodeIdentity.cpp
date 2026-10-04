/**
 * @file NodeIdentity.cpp
 * GTest unit tests for the wallet's node-identity helpers and the pure
 * decision logic behind resolveNodeIdentity().
 *
 * Two groups of functions share the NodeIdentity table:
 *   - readNodeIdentity() / storeNodeIdentity() / clearNodeIdentity() are the
 *     wallet-layer primitives xrpld composes at startup.
 *   - getNodeIdentity(session&) is the read-or-mint helper. It persists a
 *     freshly minted pair, so a second call after a mint returns the same
 *     key: that is the property that keeps a node's identity stable across
 *     restarts.
 *
 * parseNodeIdentitySeed() and selectNodeIdentity() are the libxrpl-level
 * decision helpers that xrpld's resolveNodeIdentity() marshals its inputs
 * into. Every outcome branch of resolveNodeIdentity() reduces to one of
 * these two, so testing them here covers the decision tree without an
 * xrpld Config.
 *
 * Each database-backed test gets its own file in a temporary directory, so
 * nothing here depends on order or on the developer's data directory.
 */

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/server/Wallet.h>

#include <gtest/gtest.h>
#include <soci/into.h>
#include <soci/session.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

using namespace xrpl;

namespace {

/**
 * A wallet database in its own temporary directory, removed on destruction.
 *
 * `makeTestWalletDB()` creates the schema, so every fixture starts with an
 * empty `NodeIdentity` table.
 */
class TempWalletDb
{
public:
    explicit TempWalletDb(std::string const& name)
        : dir_(std::filesystem::temp_directory_path() / ("xrpl-node-identity-" + name))
    {
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);

        DatabaseCon::Setup setup;
        setup.dataDir = dir_;
        db_ = makeTestWalletDB(setup, "wallet.db", beast::Journal{beast::Journal::getNullSink()});
    }

    ~TempWalletDb()
    {
        db_.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    TempWalletDb(TempWalletDb const&) = delete;
    TempWalletDb&
    operator=(TempWalletDb const&) = delete;

    [[nodiscard]] DatabaseCon&
    operator*() const noexcept
    {
        return *db_;
    }

private:
    std::filesystem::path dir_;
    std::unique_ptr<DatabaseCon> db_;
};

}  // namespace

TEST(WalletNodeIdentity, store_then_read_returns_the_same_pair)
{
    // The store step exists so a key minted before the Application is built
    // can be persisted afterwards. Reading it back must give the same pair, or
    // the two halves of one run report two identities.
    TempWalletDb const wallet("store-then-read");
    auto const minted = randomKeyPair(KeyType::Secp256k1);

    {
        auto db = (*wallet).checkoutDb();
        ASSERT_FALSE(readNodeIdentity(*db).has_value()) << "a fresh wallet must hold no identity";
        storeNodeIdentity(*db, minted);
    }

    auto db = (*wallet).checkoutDb();
    auto const stored = readNodeIdentity(*db);
    ASSERT_TRUE(stored.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): presence asserted above.
    EXPECT_EQ(stored->first, minted.first);
    EXPECT_TRUE(std::ranges::equal(stored->second, minted.second));
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(WalletNodeIdentity, store_appends_rather_than_replacing)
{
    // Catches storeNodeIdentity being changed into an UPSERT: the wallet
    // helper deliberately inserts without clearing, and the caller
    // (getNodeIdentity(session&) or resolveNodeIdentity+setup) is what makes
    // sure the table is empty first. Two stores must leave two rows.
    TempWalletDb const wallet("store-appends");
    auto const first = randomKeyPair(KeyType::Secp256k1);
    auto const second = randomKeyPair(KeyType::Secp256k1);
    ASSERT_NE(first.first, second.first)
        << "the two pairs must differ for this test to mean anything";

    auto db = (*wallet).checkoutDb();
    storeNodeIdentity(*db, first);
    storeNodeIdentity(*db, second);

    int rowCount = 0;
    *db << "SELECT COUNT(*) FROM NodeIdentity;", soci::into(rowCount);
    EXPECT_EQ(rowCount, 2) << "storeNodeIdentity must not clear the table";

    // The read has no ORDER BY, so pin only that ONE of the two stored keys
    // comes back -- not which one.
    auto const stored = readNodeIdentity(*db);
    ASSERT_TRUE(stored.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): presence asserted above.
    EXPECT_TRUE(stored->first == first.first || stored->first == second.first)
        << "readNodeIdentity must return one of the two stored pairs";
}

TEST(WalletNodeIdentity, clear_then_store_installs_the_new_pair)
{
    // --newnodeid clears the row and then persists the freshly minted pair.
    // Both steps are needed: clearing alone would leave the node with no
    // stored identity at all.
    TempWalletDb const wallet("clear-then-store");
    auto db = (*wallet).checkoutDb();

    auto const first = getNodeIdentity(*db);
    auto const replacement = randomKeyPair(KeyType::Secp256k1);
    ASSERT_NE(first.first, replacement.first);

    clearNodeIdentity(*db);
    EXPECT_FALSE(readNodeIdentity(*db).has_value()) << "clear must leave the table empty";

    storeNodeIdentity(*db, replacement);
    auto const stored = readNodeIdentity(*db);
    ASSERT_TRUE(stored.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): presence asserted above.
    EXPECT_EQ(stored->first, replacement.first);
    EXPECT_TRUE(std::ranges::equal(stored->second, replacement.second));
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(WalletNodeIdentity, get_mints_and_persists_when_the_table_is_empty)
{
    // The mint path must persist, not just return: a second call has to give
    // the same key. This is the property --newnodeid relies on to be
    // meaningful, and the one a caller that only reads would break.
    TempWalletDb const wallet("mint-and-persist");
    auto db = (*wallet).checkoutDb();

    auto const minted = getNodeIdentity(*db);

    auto const stored = readNodeIdentity(*db);
    ASSERT_TRUE(stored.has_value()) << "getNodeIdentity() must persist what it mints";
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): presence asserted above.
    EXPECT_EQ(stored->first, minted.first);
    EXPECT_EQ(getNodeIdentity(*db).first, minted.first);
}

// -----------------------------------------------------------------------------
// parseNodeIdentitySeed()
//
// The seed-parsing half of resolveNodeIdentity(). Each test drives one branch
// of the review's outcome list: cmdline valid, cmdline malformed, config valid,
// config malformed, neither, and cmdline-wins-over-config.
// -----------------------------------------------------------------------------

namespace {

// A base58 seed known to parse (from the "masterpassphrase" node in
// src/test/protocol/Seed_test.cpp).
constexpr auto kValidSeed = "snoPBrXtMeMyMHUVTgbuqAfg1SUTb";

// Public key derived from kValidSeed (secp256k1), same source.
constexpr auto kValidSeedPublic = "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9";

// A second base58 seed to prove cmdline-wins-over-config.
constexpr auto kOtherSeed = "snMKnVku798EnBwUfxeSD8953sLYA";

}  // namespace

TEST(ParseNodeIdentitySeed, both_absent_returns_nullopt)
{
    // Catches replacing the fallthrough with a throw, or making it mint a
    // random seed. resolveNodeIdentity() then falls through to the wallet.
    EXPECT_FALSE(parseNodeIdentitySeed(std::nullopt, std::nullopt).has_value());
}

TEST(ParseNodeIdentitySeed, valid_cmdline_returns_that_seed)
{
    // Catches swapping parseGenericSeed to always return nullopt, or reading
    // configSeed instead of cmdlineSeed. Pin the concrete public key derived
    // from the seed so the returned optional cannot silently be a different
    // valid seed.
    auto const seed = parseNodeIdentitySeed(std::string{kValidSeed}, std::nullopt);
    ASSERT_TRUE(seed.has_value());

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): presence asserted above.
    auto const sk = generateSecretKey(KeyType::Secp256k1, *seed);
    auto const pk = derivePublicKey(KeyType::Secp256k1, sk);
    EXPECT_EQ(toBase58(TokenType::NodePublic, pk), std::string{kValidSeedPublic});
}

TEST(ParseNodeIdentitySeed, valid_config_returns_that_seed)
{
    // Catches ignoring the config branch. Same public-key pin as above so the
    // result cannot silently drift to another seed.
    auto const seed = parseNodeIdentitySeed(std::nullopt, std::string{kValidSeed});
    ASSERT_TRUE(seed.has_value());

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): presence asserted above.
    auto const sk = generateSecretKey(KeyType::Secp256k1, *seed);
    auto const pk = derivePublicKey(KeyType::Secp256k1, sk);
    EXPECT_EQ(toBase58(TokenType::NodePublic, pk), std::string{kValidSeedPublic});
}

TEST(ParseNodeIdentitySeed, malformed_cmdline_throws)
{
    // Catches removing the throw. An empty string flunks parseGenericSeed(
    // rfc1751=false) because the first check inside is str.empty(). A base58
    // public key would too, but empty is the shorter probe.
    EXPECT_THROW(
        static_cast<void>(parseNodeIdentitySeed(std::string{}, std::nullopt)), std::runtime_error);
}

TEST(ParseNodeIdentitySeed, malformed_config_throws)
{
    // Catches removing the throw. "garbage" is neither valid base58 nor a
    // valid seed encoding, so parseBase58<Seed> returns nullopt and the
    // config branch throws.
    EXPECT_THROW(
        static_cast<void>(parseNodeIdentitySeed(std::nullopt, std::string{"garbage"})),
        std::runtime_error);
}

TEST(ParseNodeIdentitySeed, cmdline_wins_over_config)
{
    // Catches swapping the two if-branches. Passing DIFFERENT valid seeds on
    // each input and asserting the returned seed derives to kValidSeed's
    // public key proves which one won.
    auto const seed = parseNodeIdentitySeed(std::string{kValidSeed}, std::string{kOtherSeed});
    ASSERT_TRUE(seed.has_value());

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): presence asserted above.
    auto const sk = generateSecretKey(KeyType::Secp256k1, *seed);
    auto const pk = derivePublicKey(KeyType::Secp256k1, sk);
    EXPECT_EQ(toBase58(TokenType::NodePublic, pk), std::string{kValidSeedPublic});
}

// -----------------------------------------------------------------------------
// selectNodeIdentity()
//
// The decision half of resolveNodeIdentity(). Every remaining branch from the
// review's outcome list reduces to one of these four cases, because
// storedIdentity() catches its own exceptions and returns std::nullopt for
// every failure mode (standalone non-Load, wallet absent, invalid row, read
// throws) -- see storedIdentity() in NodeIdentity.cpp.
// -----------------------------------------------------------------------------

namespace {

// Reader that records whether it was called. resolveNodeIdentity()'s reader
// is a filesystem-touching lambda, so pinning "was it consulted" catches the
// mutations that flip which cases open the wallet.
struct TrackingReader
{
    bool called{false};
    std::optional<std::pair<PublicKey, SecretKey>> value;

    std::function<std::optional<std::pair<PublicKey, SecretKey>>()>
    fn()
    {
        return [this] {
            called = true;
            return value;
        };
    }
};

}  // namespace

TEST(SelectNodeIdentity, seed_wins_and_reader_not_consulted)
{
    // Catches removing the seed branch (or checking newNodeId first). The
    // returned pair must be keysFromSeed(kValidSeed); if the seed branch is
    // gone the reader gets called and its recorded pair or a fresh mint
    // comes back instead.
    TrackingReader reader;
    reader.value = randomKeyPair(KeyType::Secp256k1);

    auto const seed = parseBase58<Seed>(std::string{kValidSeed});
    ASSERT_TRUE(seed.has_value());

    auto const result = selectNodeIdentity(seed, /*newNodeId=*/false, reader.fn());

    EXPECT_FALSE(reader.called) << "the reader must not run when a seed is configured";
    EXPECT_EQ(toBase58(TokenType::NodePublic, result.first), std::string{kValidSeedPublic});
}

TEST(SelectNodeIdentity, newnodeid_mints_fresh_and_skips_reader)
{
    // Catches removing the `!newNodeId` guard. With a stored pair available,
    // the mint path must still run and the stored pair must not come back.
    TrackingReader reader;
    reader.value = randomKeyPair(KeyType::Secp256k1);
    auto const storedPair = *reader.value;

    auto const result = selectNodeIdentity(std::nullopt, /*newNodeId=*/true, reader.fn());

    EXPECT_FALSE(reader.called) << "--newnodeid must not open the wallet";
    EXPECT_NE(result.first, storedPair.first) << "the stored pair must be discarded";
}

TEST(SelectNodeIdentity, returns_stored_when_reader_has_one)
{
    // Catches replacing the stored-return with a mint. Also catches the
    // reader being called but its result discarded.
    TrackingReader reader;
    reader.value = randomKeyPair(KeyType::Secp256k1);
    auto const storedPair = *reader.value;

    auto const result = selectNodeIdentity(std::nullopt, /*newNodeId=*/false, reader.fn());

    EXPECT_TRUE(reader.called);
    EXPECT_EQ(result.first, storedPair.first);
    EXPECT_TRUE(std::ranges::equal(result.second, storedPair.second));
}

TEST(SelectNodeIdentity, mints_when_nothing_stored)
{
    // Catches removing the mint fallback. With the reader returning
    // nullopt, selectNodeIdentity must still produce a keypair, and it must
    // differ from anything it could have accidentally reused.
    TrackingReader reader;  // value stays std::nullopt.

    auto const result = selectNodeIdentity(std::nullopt, /*newNodeId=*/false, reader.fn());

    EXPECT_TRUE(reader.called);
    // The mint path is randomKeyPair(), so the two calls must yield distinct
    // keys. Same probe the wallet-side tests use.
    auto const another = randomKeyPair(KeyType::Secp256k1);
    EXPECT_NE(result.first, another.first);
}
